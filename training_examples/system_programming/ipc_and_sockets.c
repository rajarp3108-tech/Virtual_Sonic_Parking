/*
 * ipc_and_sockets.c - pipes, a message queue, shared memory with a
 * semaphore, and a UDP echo next to the TCP status path the project uses.
 *
 * Build:  make -C training_examples
 * Run:    ./build/training/ipc_and_sockets
 *
 * Relationship to the project: the monitor's logger uses a *thread* queue
 * rather than an IPC mechanism, because threads in one process are cheaper
 * than any IPC. This file shows the alternatives, so the choice can be
 * justified rather than guessed.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

/* ---------------------------------------------------------------- */
/* 1. pipe(): unidirectional byte stream between related processes   */
/* ---------------------------------------------------------------- */

static void demo_pipe(void)
{
	printf("\n=== 1. pipe() - a byte stream, one direction ===\n");
	fflush(stdout);

	int fds[2];
	if (pipe(fds) == -1) {
		perror("pipe");
		return;
	}

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return;
	}

	if (pid == 0) {
		close(fds[0]); /* the child only writes */
		const char* messages[] = {"distance=140\n", "distance=130\n",
					  "distance=120\n"};
		for (int i = 0; i < 3; ++i) {
			if (write(fds[1], messages[i], strlen(messages[i])) == -1)
				perror("write");
		}
		close(fds[1]);
		_exit(0);
	}

	close(fds[1]); /* the parent only reads */
	char buffer[64];
	ssize_t n;
	int lines = 0;
	while ((n = read(fds[0], buffer, sizeof(buffer) - 1)) > 0) {
		buffer[n] = '\0';
		printf("  parent read: %s", buffer);
		++lines;
	}
	close(fds[0]);
	waitpid(pid, NULL, 0);
	printf("  lines received: %d (read() returned 0 at end of file)\n",
	       lines);
}

/* ---------------------------------------------------------------- */
/* 2. System V message queue                                        */
/* ---------------------------------------------------------------- */

struct message {
	long type;
	char text[64];
};

static void demo_message_queue(void)
{
	printf("\n=== 2. System V message queue ===\n");
	fflush(stdout);

	/* ftok() needs an existing path; /tmp is always there. The key is
	 * derived from the inode number and the device number, so it is
	 * stable across runs and does not collide with other programs that
	 * pick a different project id. */
	key_t key = ftok("/tmp", 'P');
	if (key == -1) {
		perror("ftok");
		return;
	}

	int msgid = msgget(key, IPC_CREAT | 0600);
	if (msgid == -1) {
		perror("msgget");
		return;
	}

	struct message m;
	m.type = 1;
	snprintf(m.text, sizeof(m.text), "state=DANGER");
	if (msgsnd(msgid, &m, strlen(m.text) + 1, 0) == -1)
		perror("msgsnd");

	struct message received;
	ssize_t n = msgrcv(msgid, &received, sizeof(received.text), 1, 0);
	if (n > 0)
		printf("  received from the queue: %s\n", received.text);
	else
		perror("msgrcv");

	/* Remove it, otherwise it stays in the kernel forever. */
	msgctl(msgid, IPC_RMID, NULL);
	printf("  queue removed with IPC_RMID\n");
}

/* ---------------------------------------------------------------- */
/* 3. Shared memory guarded by a semaphore                           */
/* ---------------------------------------------------------------- */

struct shared_area {
	int distance_cm;
	int writes;
};

static void demo_shared_memory(void)
{
	printf("\n=== 3. shared memory + semaphore (Process Shared) ===\n");
	fflush(stdout);

	int shm_id = shmget(IPC_PRIVATE, sizeof(struct shared_area), IPC_CREAT | 0600);
	if (shm_id == -1) {
		perror("shmget");
		return;
	}

	/* The semaphore is opened with IPC_PRIVATE, which in POSIX means
	 * "make a new one"; the returned id is how the other process finds
	 * it, so in a real program the parent would pass it in shared memory
	 * or through a file. */
	int sem_id = semget(IPC_PRIVATE, 2, IPC_CREAT | 0600);
	if (sem_id == -1) {
		perror("semget");
		shmctl(shm_id, IPC_RMID, NULL);
		return;
	}

	struct shared_area* area = shmat(shm_id, NULL, 0);
	if (area == (void*)-1) {
		perror("shmat");
		semctl(sem_id, 0, IPC_RMID);
		shmctl(shm_id, IPC_RMID, NULL);
		return;
	}

	/* SETVAL on semaphore number 0 makes the binary lock start free
	 * (value 1). The second slot is unused. */
	if (semctl(sem_id, 0, SETVAL, 1) == -1)
		perror("semctl SETVAL");

	area->distance_cm = 150;
	area->writes = 0;

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
	} else if (pid == 0) {
		for (int i = 0; i < 5; ++i) {
			/* P operation: sem_op = -1 blocks until the value is
			 * positive, then decrements it. With an initial value
			 * of 1 that is a plain mutual-exclusion lock. */
			struct sembuf wait_lock = {0, -1};
			struct sembuf unlock_lock = {0, 1};
			semop(sem_id, &wait_lock, 1);
			area->distance_cm -= 10;
			area->writes++;
			semop(sem_id, &unlock_lock, 1);
		}
		_exit(0);
	} else {
		struct sembuf wait_lock = {0, -1};
		struct sembuf unlock_lock = {0, 1};
		for (int i = 0; i < 5; ++i) {
			semop(sem_id, &wait_lock, 1);
			area->writes++;
			semop(sem_id, &unlock_lock, 1);
		}
		waitpid(pid, NULL, 0);
		printf("  shared distance_cm = %d, total writes = %d\n",
		       area->distance_cm, area->writes);
	}

	shmdt(area);
	semctl(sem_id, 0, IPC_RMID);
	shmctl(shm_id, IPC_RMID, NULL);
	printf("  both objects removed\n");
}

/* ---------------------------------------------------------------- */
/* 4. UDP echo, next to the project's TCP path                      */
/* ---------------------------------------------------------------- */

static void demo_udp_echo(void)
{
	printf("\n=== 4. UDP echo - connectionless, same port number as TCP ===\n");
	fflush(stdout);

	int server = socket(AF_INET, SOCK_DGRAM, 0);
	if (server == -1) {
		perror("socket");
		return;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = 0; /* let the kernel choose a free port */

	if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		perror("bind");
		close(server);
		return;
	}

	socklen_t len = sizeof(addr);
	if (getsockname(server, (struct sockaddr*)&addr, &len) == -1) {
		perror("getsockname");
		close(server);
		return;
	}
	printf("  UDP server bound to 127.0.0.1:%d\n", ntohs(addr.sin_port));

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
	} else if (pid == 0) {
		/* server */
		char buffer[128];
		struct sockaddr_in client;
		socklen_t client_len = sizeof(client);
		ssize_t n = recvfrom(server, buffer, sizeof(buffer) - 1, 0,
				     (struct sockaddr*)&client, &client_len);
		if (n > 0) {
			buffer[n] = '\0';
			printf("  server received: %s", buffer);
			const char* reply = "STATUS distance=42 state=WARNING\n";
			sendto(server, reply, strlen(reply), 0,
			       (struct sockaddr*)&client, client_len);
		}
		close(server);
		_exit(0);
	} else {
		/* client */
		const char* request = "STATUS?\n";
		sendto(server, request, strlen(request), 0,
		       (struct sockaddr*)&addr, sizeof(addr));

		char buffer[128];
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		setsockopt(server, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

		ssize_t n = recv(server, buffer, sizeof(buffer) - 1, 0);
		if (n > 0) {
			buffer[n] = '\0';
			printf("  client received: %s", buffer);
		} else {
			printf("  no reply (timeout)\n");
		}
		waitpid(pid, NULL, 0);
	}

	close(server);
	printf("  compare with the project's TCP server: same payload, but\n"
	       "  UDP has no handshake, no ordering guarantee and no\n"
	       "  reconnection - which is why the monitor uses TCP.\n");
}

int main(void)
{
	printf("IPC and sockets - training demonstration\n");
	printf("(not part of the parking monitor runtime)\n");

	demo_pipe();
	demo_message_queue();
	demo_shared_memory();
	demo_udp_echo();

	printf("\nIPC and sockets demonstration completed.\n");
	return EXIT_SUCCESS;
}
