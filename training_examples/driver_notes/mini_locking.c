/*
 * mini_locking.c - user-space versions of the three synchronisation
 * primitives the driver syllabus asks for, so the ideas can be run and
 * observed before they are used in kernel code.
 *
 * Build:  make -C training_examples
 * Run:    ./build/training/mini_locking
 *
 * The driver itself uses a kernel mutex (see driver/parking_sensor.c and
 * training_examples/driver_notes/driver_notes.md). This file exists so the
 * three primitives can be compared side by side:
 *
 *   pthread_mutex_t  - sleeps, may be slow, the general-purpose choice
 *   pthread_spinlock  - does not sleep, only for very short critical sections
 *   C11 atomic        - one value, no lock, lock-free on x86 and ARMv8
 *
 * Correctness note: the same shared counter is incremented by every worker in
 * every demo. The "lost updates" column of the summary is what a correct
 * implementation must produce. If a demo's total is wrong, the locking is
 * wrong, which is the point of running it.
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS 4
#define ITERATIONS 20000
#define SPIN_ITERS 64

/* ---------------------------------------------------------------- */
/* 1. pthread_mutex_t                                                */
/* ---------------------------------------------------------------- */

static long shared_counter = 0;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

static void* mutex_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i) {
		/* pthread_mutex_lock may sleep, so this is safe even if the
		 * critical section is long or allocates memory. */
		pthread_mutex_lock(&counter_mutex);
		shared_counter++;
		pthread_mutex_unlock(&counter_mutex);
	}
	return NULL;
}

static void demo_mutex(void)
{
	printf("\n=== 1. pthread_mutex_t ===\n");
	shared_counter = 0;

	pthread_t threads[THREADS];
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&threads[i], NULL, mutex_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(threads[i], NULL);

	printf("  expected %d, got %ld  %s\n", THREADS * ITERATIONS,
	       shared_counter,
	       shared_counter == THREADS * ITERATIONS ? "correct" : "LOST UPDATES");
	printf("  may sleep while holding the lock - the default choice\n");
}

/* ---------------------------------------------------------------- */
/* 2. pthread_spinlock_t, and the broken version for comparison      */
/* ---------------------------------------------------------------- */

static long spin_counter = 0;
static pthread_spinlock_t spin_lock = PTHREAD_SPINLOCK_INITIALIZER;
static pthread_mutex_t slow_lock = PTHREAD_MUTEX_INITIALIZER;

static void* spin_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i) {
		pthread_spin_lock(&spin_lock);
		spin_counter++;
		pthread_spin_unlock(&spin_lock);
	}
	return NULL;
}

static void* broken_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i)
		shared_counter++; /* no lock at all: a data race */
	return NULL;
}

static void* slow_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i) {
		/* A deliberately long critical section. A spinlock here
		 * would burn its timeslice instead of sleeping. */
		pthread_mutex_lock(&slow_lock);
		for (volatile int spin = 0; spin < SPIN_ITERS; ++spin)
			;
		shared_counter++;
		pthread_mutex_unlock(&slow_lock);
	}
	return NULL;
}

static void demo_spinlock(void)
{
	printf("\n=== 2. pthread_spinlock_t, and what happens without a lock ===\n");

	spin_counter = 0;
	pthread_t threads[THREADS];
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&threads[i], NULL, spin_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(threads[i], NULL);
	printf("  spinlock:   expected %d, got %ld  %s\n",
	       THREADS * ITERATIONS, spin_counter,
	       spin_counter == THREADS * ITERATIONS ? "correct" : "WRONG");

	shared_counter = 0;
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&threads[i], NULL, broken_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(threads[i], NULL);
	printf("  no lock:    expected %d, got %ld  %s\n",
	       THREADS * ITERATIONS, shared_counter,
	       shared_counter == THREADS * ITERATIONS
		       ? "correct this time by luck"
		       : "LOST UPDATES (a data race)");

	shared_counter = 0;
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&threads[i], NULL, slow_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(threads[i], NULL);
	printf("  long section with a mutex: got %ld  %s\n", shared_counter,
	       shared_counter == THREADS * ITERATIONS ? "correct" : "LOST UPDATES");
	printf("  a spinlock on a long section would busy-wait; a mutex sleeps\n");
}

/* ---------------------------------------------------------------- */
/* 3. C11 atomics: the lock-free version                              */
/* ---------------------------------------------------------------- */

static atomic_long atomic_counter = 0;

static void* atomic_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i)
		atomic_fetch_add(&atomic_counter, 1); /* one instruction */
	return NULL;
}

static void demo_atomic(void)
{
	printf("\n=== 3. C11 atomics ===\n");
	atomic_store(&atomic_counter, 0);

	pthread_t threads[THREADS];
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&threads[i], NULL, atomic_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(threads[i], NULL);

	long total = atomic_load(&atomic_counter);
	printf("  expected %d, got %ld  %s\n", THREADS * ITERATIONS, total,
	       total == THREADS * ITERATIONS ? "correct" : "WRONG");
	printf("  lock-free on x86-64 and ARMv8; no lock, but it only protects\n"
	       "  this one value and cannot guard a multi-step sequence\n");
}

/* ---------------------------------------------------------------- */
/* 4. The project's actual pattern                                    */
/* ---------------------------------------------------------------- */

static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static int distance_cm = 150;

static void* sensor_worker(void* arg)
{
	(void)arg;
	for (int i = 0; i < ITERATIONS; ++i) {
		pthread_mutex_lock(&state_mutex);
		distance_cm--; /* several fields change together: must be atomic */
		pthread_mutex_unlock(&state_mutex);
	}
	return NULL;
}

static void* monitor_worker(void* arg)
{
	(void)arg;
	long inconsistent = 0;
	for (int i = 0; i < ITERATIONS; ++i) {
		int snapshot;
		pthread_mutex_lock(&state_mutex);
		snapshot = distance_cm;
		pthread_mutex_unlock(&state_mutex);
		/* The value is copied under the same lock that updates it, so
		 * this is a consistent snapshot. Reading it without the lock
		 * would be a race even for a single int on some platforms. */
		if (snapshot < 0)
			++inconsistent;
	}
	printf("  reader finished, %ld out-of-range snapshots (expected 0)\n",
	       inconsistent);
	return NULL;
}

static void demo_project_pattern(void)
{
	printf("\n=== 4. the pattern the parking monitor uses ===\n");

	pthread_mutex_lock(&state_mutex);
	distance_cm = 150;
	pthread_mutex_unlock(&state_mutex);

	pthread_t sensor[THREADS];
	pthread_t monitor;
	pthread_create(&monitor, NULL, monitor_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_create(&sensor[i], NULL, sensor_worker, NULL);
	for (int i = 0; i < THREADS; ++i)
		pthread_join(sensor[i], NULL);
	pthread_join(monitor, NULL);

	pthread_mutex_lock(&state_mutex);
	printf("  final distance_cm = %d (expected %d)\n", distance_cm,
	       150 - THREADS * ITERATIONS);
	pthread_mutex_unlock(&state_mutex);
	printf("  a mutex, not an atomic: distance, state, tick count and the\n"
	       "  paused flag are updated together and must be seen together.\n");
}

int main(void)
{
	printf("Synchronisation primitives - training demonstration\n");
	printf("(not part of the parking monitor runtime)\n");

	demo_mutex();
	demo_spinlock();
	demo_atomic();
	demo_project_pattern();

	printf("\nSynchronisation demonstration completed.\n");
	return EXIT_SUCCESS;
}
