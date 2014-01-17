/*
 * Copyright 2014 Frantisek Hrbata <fhrbata@pluginfs.org>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include <pthread.h>
#include <limits.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <avplg.h>

#define THREADS_COUNT 10

static const char *version = "0.1";

static struct avplg_connection con;
static int stop = 0;

static void sighandler(int sig)
{
	stop = 1;
}

static int check(void)
{
	struct avplg_event event;
	char fn[PATH_MAX];

	while (!stop) {
		if (avplg_request(&con, &event, 500)) {
			if (errno == ETIMEDOUT)
				continue;

			perror("avplg_request failed");
			return -1;
		}

		if (avplg_get_filename(&event, fn, PATH_MAX)) {
			perror("avplg_get_filename failed");
			return -1;
		}

		if (avplg_set_result(&event, AVPLG_ACCESS_ALLOW)) {
			perror("avplg_set_result failed");
			return -1;
		}

		printf("thread[%lu]: id: %lu, type: %d, fd: %d, pid: %d, "
				"tgid: %d, res: %d, fn: %s\n", pthread_self(),
				event.id, event.type, event.fd, event.pid,
				event.tgid, event.res, fn);

		if (avplg_reply(&con, &event)) {
			perror("avplg_reply failed");
			return -1;
		}

	}

	return 0;
}

static void *check_thread(void *data)
{
	sigset_t sigmask;

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGINT);
	sigaddset(&sigmask, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

	if (check())
		fprintf(stderr, "thread[%lu] unexpectedly stopped, %s\n",
				pthread_self(), strerror(errno));

	return NULL;
}

int main(int argc, char *argv[])
{

	pthread_t threads[THREADS_COUNT];
	struct sigaction sa;
	int i;
	int rv;

	printf("avtest: version %s\n", version);
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sighandler;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGINT);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	if (avplg_register(&con)) {
		perror("avplg_register failed");
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < THREADS_COUNT; i++) {
		rv = pthread_create(&threads[i], NULL, check_thread, NULL);
		if (rv) {
			fprintf(stderr, "pthread_create failed: %d\n", rv);
			exit(EXIT_FAILURE);
		}
	}

	pause();

	for (i = 0; i < THREADS_COUNT; i++) {
		rv = pthread_join(threads[i], NULL);
		if (rv) {
			fprintf(stderr, "pthread_join failed: %d\n", rv);
			exit(EXIT_FAILURE);
		}
	}

	if (avplg_unregister(&con)) {
		perror("avplg_unregister failed");
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}

