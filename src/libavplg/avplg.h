/*
 * Copyright 2014 Frantisek Hrbata <fhrbata@pluginfs.org>
 *
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://www.wtfpl.net/ for more details.
 */

#ifndef __AVPLG_H__
#define __AVPLG_H__

#include <sys/types.h>

#define AVPLG_EVENT_OPEN  1
#define AVPLG_EVENT_CLOSE 2

#define AVPLG_ACCESS_ALLOW 1
#define AVPLG_ACCESS_DENY  2

struct avplg_connection {
	int fd;
};

struct avplg_event {
	unsigned long id;
	int type;
	int fd;
	pid_t pid;
	pid_t tgid;
	int res;
	unsigned int ver;
};

int avplg_register(struct avplg_connection *conn);
int avplg_unregister(struct avplg_connection *conn);
int avplg_register_trusted(struct avplg_connection *conn);
int avplg_unregister_trusted(struct avplg_connection *conn);
int avplg_request(struct avplg_connection *conn, struct avplg_event *event,
		int timeout);
int avplg_reply(struct avplg_connection *conn, struct avplg_event *event);
int avplg_set_result(struct avplg_event *event, int res);
int avplg_get_filename(struct avplg_event *event, char *buf, int size);

#endif
