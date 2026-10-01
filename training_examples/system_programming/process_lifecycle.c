/*
 * process_lifecycle.c - fork, exec, wait, process states and signals.
 *
 * Written in C on purpose: this is the POSIX layer that C++ sits on top of,
 * and seeing it in C makes it obvious that fork/exec/wait are not C++ ideas.
 *
 * Build:  make -C training_examples
 * Run:    ./build/training/process_lifecycle
 *
 * Relationship to the project: the `--daemon` mode of the monitor uses the
 * same double fork, and SIGINT/SIGTERM handling in main.cpp is the same
 * signal machinery.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---------------------------------------------------------------- */
/* 1. Signals: the async-safe handler and its limits                 */
/* ---------------------------------------------------------------- */

/* A volatile sig_atomic_t is the only kind of object a handler may
 * portably touch. The parking monitor uses std::atomic<bool> for the same
 * reason. */
static volatile sig_atomic_t stop_requested = 0;

static void on_terminate(int signum)
{
	(void)signum;
	stop_requested = 1; /* set a flag - do nothing else */
}

static void install_handlers(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_terminate;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0; /* no SA_RESTART: blocking calls should return EINTR */

	if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}
}

/* ---------------------------------------------------------------- */
/* 2. fork: a new process with a copy of the address space           */
/* ---------------------------------------------------------------- */

static void demo_fork(void)
{
	printf("\n=== 2. fork() ===\n");
	fflush(stdout);

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return;
	}

	if (pid == 0) {
		/* The child. It has its own copy of every variable, so
		 * changing one here does not affect the parent. */
		printf("  [child ]  pid=%d  ppid=%d\n", (int)getpid(),
		       (int)getppid());
		_exit(0); /* _exit, not exit: no atexit handlers, no double flush */
	}

	/* The parent. waitpid() reaps the child so it does not become a
	 * zombie. */
	int status = 0;
	if (waitpid(pid, &status, 0) == -1) {
		perror("waitpid");
		return;
	}

	if (WIFEXITED(status))
		printf("  [parent]  child %d exited with code %d\n", (int)pid,
		       WEXITSTATUS(status));
}

/* ---------------------------------------------------------------- */
/* 3. exec: replace the current program image                        */
/* ---------------------------------------------------------------- */

static void demo_exec(void)
{
	printf("\n=== 3. exec() replaces the program image ===\n");
	printf("  running: /bin/echo hello-from-exec\n");
	fflush(stdout);

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return;
	}

	if (pid == 0) {
		/* char* const argv[] because exec takes char* const argv[].
		 * The cast silences -Wwrite-strings. */
		char* const argv[] = {(char*)"/bin/echo", (char*)"hello-from-exec",
				     NULL};
		execv("/bin/echo", argv);
		/* exec only returns on failure. Anything here means the
		 * child is still the old program. */
		perror("execv");
		_exit(127); /* the conventional "command not found" code */
	}

	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFEXITED(status))
		printf("  [parent]  child exit code %d (127 would mean exec failed)\n",
		       WEXITSTATUS(status));
}

/* ---------------------------------------------------------------- */
/* 4. The classic double fork = a real daemon                       */
/* ---------------------------------------------------------------- */

static void daemonise(void)
{
	printf("\n=== 4. double fork -> a daemon ===\n");
	fflush(stdout);

	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return;
	}
	if (pid > 0)
		_exit(0); /* the shell's child leaves immediately */

	if (setsid() == -1) {
		perror("setsid");
		_exit(EXIT_FAILURE);
	}

	pid = fork();
	if (pid == -1) {
		perror("fork");
		_exit(EXIT_FAILURE);
	}
	if (pid > 0)
		_exit(0); /* the session leader leaves too */

	/* Now we are a session leader with no controlling terminal. */
	printf("  daemon pid = %d, session = %d, ppid = %d\n", (int)getpid(),
	       (int)getsid(0), (int)getppid());
	printf("  (a real daemon would redirect 0,1,2 and never return)\n");
}

/* ---------------------------------------------------------------- */
/* 5. Sleeping in a loop and being stopped by a signal               */
/* ---------------------------------------------------------------- */

static void demo_signal_stop(void)
{
	printf("\n=== 5. a signal interrupts the wait (send SIGTERM to %d to skip) ===\n",
	       (int)getpid());
	fflush(stdout);

	/* Kill this program from another terminal:
	 *     kill -TERM <pid>
	 * Then the loop below exits and the cleanup runs. */
	while (!stop_requested) {
		sleep(1);
		printf("  working... (stop_requested=%d)\n", (int)stop_requested);
		fflush(stdout);
	}
	printf("  signal received, shutting down cleanly\n");
}

int main(int argc, char* argv[])
{
	int wait_for_signal = 0;
	int i;

	for (i = 1; i < argc; ++i) {
		if (strcmp(argv[i], "--wait-for-signal") == 0)
			wait_for_signal = 1;
		else if (strcmp(argv[i], "--help") == 0) {
			printf("Usage: %s [--wait-for-signal]\n", argv[0]);
			return EXIT_SUCCESS;
		}
	}

	printf("Process lifecycle - training demonstration\n");
	printf("(not part of the parking monitor runtime)\n");

	install_handlers();

	printf("\n=== 1. Process identity ===\n");
	printf("  my pid   = %d\n", (int)getpid());
	printf("  my ppid  = %d\n", (int)getppid());
	printf("  my sid   = %d\n", (int)getsid(0));
	printf("  my pgid  = %d\n", (int)getpgrp());
	printf("  my uid   = %d\n", (int)getuid());

	demo_fork();
	demo_exec();
	daemonise();

	/* Only wait when asked, otherwise the program looks like it hung. */
	if (wait_for_signal) {
		printf("\n  run: kill -TERM %d   (or press Ctrl-C)\n",
		       (int)getpid());
		demo_signal_stop();
	} else {
		printf("\n  (re-run with --wait-for-signal to see case 5)\n");
	}

	printf("\nProcess lifecycle demonstration completed.\n");
	return EXIT_SUCCESS;
}
