/*
 * Copyright 2014 Frantisek Hrbata <fhrbata@pluginfs.org>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "avplg.h"

static int avplg_open_con(struct avplg_connection *con, int flags)
{
	if (!con) {
		errno = EINVAL;
		return -1;
	}

	if ((con->fd = open("/dev/avplg", flags)) == -1)
		return -1;

	return 0;
}

int avplg_register(struct avplg_connection *con)
{
	return avplg_open_con(con, O_RDWR);
}

int avplg_unregister(struct avplg_connection *con)
{
	if (!con) {
		errno = EINVAL;
		return -1;
	}

	if (close(con->fd) == -1)
		return -1;

	return 0;
}

int avplg_register_trusted(struct avplg_connection *con)
{
	return avplg_open_con(con, O_RDONLY);
}

int avplg_unregister_trusted(struct avplg_connection *con)
{
	return avplg_unregister(con);
}

int avplg_request(struct avplg_connection *con, struct avplg_event *event, int timeout)
{
	struct timeval tv;
	struct timeval *ptv;
	char buf[256];
	fd_set rfds;
	int rv = 0;

	if (!con || !event || timeout < 0) {
		errno = EINVAL;
		return -1;
	}

	FD_ZERO(&rfds);
	FD_SET(con->fd, &rfds);

	if (timeout) {
		tv.tv_sec = timeout / 1000;
		tv.tv_usec = (timeout - (tv.tv_sec * 1000)) * 1000;
		ptv = &tv;
	} else
		ptv = NULL;

	while (!rv) {
		rv = select(con->fd + 1, &rfds, NULL, NULL, ptv);
		if (rv == 0) {
			errno = ETIMEDOUT;
			return -1;
		}
		if (rv == -1)
			return -1;

		rv = read(con->fd, buf, 256);
		if (rv == -1)
			return -1;
	}

	if (sscanf(buf, "ver:%u,id:%lu,type:%d,fd:%d,pid:%d,tgid:%d",
				&event->ver, &event->id, &event->type,
				&event->fd, &event->pid, &event->tgid) != 6)
		return -1;

	event->res = 0;

	return 0;
}

int avplg_reply(struct avplg_connection *con, struct avplg_event *event)
{
	char buf[256];

	if (!con || !event) {
		errno = EINVAL;
		return -1;
	}

	snprintf(buf, 256, "ver:%u,id:%lu,res:%d", event->ver, event->id,
			event->res);

	if (write(con->fd, buf, strlen(buf) + 1) == -1)
		return -1;

	if (close(event->fd) == -1)
		return -1;

	return 0;
}

int avplg_set_result(struct avplg_event *event, int res)
{
	if (!event) {
		errno = EINVAL;
		return -1;
	}

	if (res != AVPLG_ACCESS_ALLOW && res != AVPLG_ACCESS_DENY) {
		errno = EINVAL;
		return -1;
	}

	event->res = res;

	return 0;
}

int avplg_get_filename(struct avplg_event *event, char *buf, int size)
{
	char fn[256];

	if (!event || !buf) {
		errno = EINVAL;
		return -1;
	}

	memset(fn, 0, 256);
	memset(buf, 0, size);
	snprintf(fn, 255, "/proc/%d/fd/%d", getpid(), event->fd);

	if (readlink(fn, buf, size - 1) == -1)
		return -1;

	return 0;
}
