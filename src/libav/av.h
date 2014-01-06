/*
 * Copyright 2014 Frantisek Hrbata <fhrbata@pluginfs.org>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef __AV_H__
#define __AV_H__

#include <sys/types.h>

#define AV_EVENT_OPEN  1
#define AV_EVENT_CLOSE 2

#define AV_ACCESS_ALLOW 1
#define AV_ACCESS_DENY  2

struct av_connection {
	int fd;
};

struct av_event {
	unsigned long id;
	int type;
	int fd;
	pid_t pid;
	pid_t tgid;
	int res;
	unsigned int ver;
};

int av_register(struct av_connection *conn);
int av_unregister(struct av_connection *conn);
int av_register_trusted(struct av_connection *conn);
int av_unregister_trusted(struct av_connection *conn);
int av_request(struct av_connection *conn, struct av_event *event, int timeout);
int av_reply(struct av_connection *conn, struct av_event *event);
int av_set_result(struct av_event *event, int res);
int av_get_filename(struct av_event *event, char *buf, int size);

#endif
