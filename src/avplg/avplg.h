/*
 * Copyright 2013 Frantisek Hrbata <fhrbata@pluginfs.org>
 * 
 * This file is part of PluginFS.
 *
 * PluginFS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PluginFS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PluginFS.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _AVPLG_H
#define _AVPLG_H

#include <linux/device.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <pluginfs.h>

#define AVPLG_VERSION   "0.1" 

#define AVPLG_EVENT_OPEN 1
#define AVPLG_EVENT_CLOSE 2

#define AVPLG_ACCESS_ALLOW 1
#define AVPLG_ACCESS_DENY 2

#define AVPLG_PROT_VERSION 1

extern int avplg_chrdev_init(void);
extern void avplg_chrdev_exit(void);

struct avplg_event {
	struct list_head list;
	struct list_head task_event;
	atomic_t count;
	struct completion wait;
	int result;
	unsigned long id;
	int type;
	pid_t pid;
	pid_t tgid;
	int fd;
	struct path path;
	struct file *file;
	unsigned long result_ver;
	unsigned long cache_glob_ver;
	int plg_id;
};

extern int avplg_event_process(struct file *file, int type, int id);
extern int avplg_event_init(void);
extern void avplg_event_exit(void);
extern struct avplg_event *avplg_event_get(struct avplg_event *event);
extern void avplg_event_put(struct avplg_event *event);
extern void avplg_event_start(void);
extern void avplg_event_stop(void);
extern void avplg_event_done(struct avplg_event *event);
extern struct avplg_event *avplg_event_pop(void);
extern void avplg_event_push(struct avplg_event *event);
extern int avplg_event_get_file(struct avplg_event *event);
extern void avplg_event_put_file(struct avplg_event *event);
extern ssize_t avplg_event2buf(char __user *buf, size_t size,
		struct avplg_event *event);
extern struct avplg_event *avplg_buf2event(const char __user *buf, size_t size);
extern int avplg_event_empty(void);

extern wait_queue_head_t avplg_event_available;

struct avplg_task {
	struct list_head list;
	struct list_head events;
	spinlock_t lock;
	atomic_t count;
	unsigned long event_ids;
	pid_t tgid;
	unsigned int opened;
};

extern struct avplg_task *avplg_task_get(struct avplg_task *task);
extern void avplg_task_put(struct avplg_task *task);
extern int avplg_task_add(pid_t tgid);
extern int avplg_task_rem(pid_t tgid);
extern struct avplg_task *avplg_task_find(pid_t tgid);
extern int avplg_task_allow(pid_t tgid);
extern int avplg_task_add_event(struct avplg_event *event);
extern int avplg_task_rem_event(struct avplg_event *event);
extern struct avplg_event *avplg_task_pop_event(struct avplg_task *task,
		unsigned long id);
extern int avplg_task_empty(void);

struct avplg_trusted {
	struct list_head list;
	pid_t tgid;
	unsigned int opened;
};

extern int avplg_trusted_add(pid_t tgid);
extern int avplg_trusted_rem(pid_t tgid);
extern int avplg_trusted_allow(pid_t tgid);

#define AVPLG_NOCLOSE 1
#define AVPLG_NOCACHE 2
#define AVPLG_NOWRITE 4

struct avplg_sb_info {
	unsigned long jiffies;
	unsigned long cache_ver;
	unsigned int flags;
};

extern struct avplg_sb_info *avplg_sbi(struct super_block *sb, int id);

extern unsigned int avplg_sb_noclose(struct avplg_sb_info *sbi);
extern unsigned int avplg_sb_nocache(struct avplg_sb_info *sbi);
extern unsigned int avplg_sb_nowrite(struct avplg_sb_info *sbi);
extern unsigned long avplg_sb_timeout(struct avplg_sb_info *sbi);

struct avplg_inode_info {
	spinlock_t lock;
	int result;
	unsigned long result_ver;
	unsigned long cache_ver;
	unsigned long cache_sb_ver;
};

extern struct avplg_inode_info *avplg_ii(struct inode *i, int id);

extern unsigned long avplg_sb_cache_ver(struct avplg_sb_info *sbi);
extern void avplg_sb_cache_inv(struct avplg_sb_info *sbi);
extern void avplg_icache_update(struct avplg_event *event);
extern int avplg_icache_check(struct file *file, int id);
extern void avplg_icache_inv(struct file *file, int id);

#endif
