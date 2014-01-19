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

#include "avplg.h"

DECLARE_WAIT_QUEUE_HEAD(avplg_event_available);
static LIST_HEAD(avplg_event_list);
static DEFINE_SPINLOCK(avplg_event_lock);
static int avplg_event_accept = 0;
static struct kmem_cache *avplg_event_cache = NULL;

static struct avplg_event *avplg_event_alloc(struct file *file, int type,
		int id)
{
	struct avplg_event *event;
	struct avplg_inode_info *ii;
	struct avplg_sb_info *sbi;

	event = kmem_cache_zalloc(avplg_event_cache, GFP_KERNEL);
	if (!event)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&event->list);
	INIT_LIST_HEAD(&event->task_event);
	atomic_set(&event->count, 1);
	init_completion(&event->wait);
	event->type = type;
	event->plg_id = id;
	event->fd = -1;
	event->pid = current->pid;
	event->tgid = current->tgid;
	event->path = file->f_path;
	path_get(&event->path);

	ii = avplg_ii(file->f_dentry->d_inode, id);
	sbi = avplg_sbi(file->f_dentry->d_sb, id);

	spin_lock(&ii->lock);
	event->result_ver = ii->cache_ver;
	event->cache_glob_ver = avplg_sb_cache_ver(sbi);
	spin_unlock(&ii->lock);

	return event;
}

struct avplg_event *avplg_event_get(struct avplg_event *event)
{
	if (!event || IS_ERR(event))
		return NULL;

	BUG_ON(!atomic_read(&event->count));
	atomic_inc(&event->count);
	return event;
}

void avplg_event_put(struct avplg_event *event)
{
	if (!event || IS_ERR(event))
		return;

	BUG_ON(!atomic_read(&event->count));

	if (!atomic_dec_and_test(&event->count))
		return;

	path_put(&event->path);
	kmem_cache_free(avplg_event_cache, event);
}

static int avplg_event_add(struct avplg_event *event)
{
	spin_lock(&avplg_event_lock);

	if (!avplg_event_accept) {
		spin_unlock(&avplg_event_lock);
		return 1;
	}

	list_add_tail(&event->list, &avplg_event_list); 

	avplg_event_get(event);

	wake_up_interruptible(&avplg_event_available);

	spin_unlock(&avplg_event_lock);

	return 0;
}

static void avplg_event_rem_nolock(struct avplg_event *event)
{
	if (list_empty(&event->list))
		return;

	list_del_init(&event->list);

	avplg_event_put(event);
}

static void avplg_event_rem(struct avplg_event *event)
{
	spin_lock(&avplg_event_lock);
	avplg_event_rem_nolock(event);
	spin_unlock(&avplg_event_lock);
}

static int avplg_event_wait(struct avplg_event *event)
{
	struct avplg_sb_info *sbi;
	long jiffies;

	sbi = avplg_sbi(event->path.dentry->d_sb, event->plg_id);

	jiffies = avplg_sb_timeout(sbi);

	jiffies = wait_for_completion_interruptible_timeout(&event->wait,
			jiffies);

	if (jiffies < 0)
		return (int)jiffies;

	if (!jiffies) {
		pr_warn("avplg: wait for reply timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int avplg_event_process(struct file *file, int type, int id)
{
	struct avplg_event *event;
	int rv = 0;

	event = avplg_event_alloc(file, type, id);
	if (IS_ERR(event))
		return PTR_ERR(event);

	if (avplg_event_add(event))
		goto exit;

	rv = avplg_event_wait(event);
	if (rv)
		goto exit;

	avplg_icache_update(event);
	rv = event->result;
exit:
	avplg_event_rem(event);
	avplg_event_put(event);
	return rv;
}

void avplg_event_start(void)
{
	spin_lock(&avplg_event_lock);
	avplg_event_accept = 1;
	spin_unlock(&avplg_event_lock);
}

void avplg_event_done(struct avplg_event *event)
{
	complete(&event->wait);
}

void avplg_event_stop(void)
{
	struct avplg_event *event;
	struct avplg_event *tmp;

	spin_lock(&avplg_event_lock);
	if (!avplg_task_empty()) {
		spin_unlock(&avplg_event_lock);
		return;
	}

	list_for_each_entry_safe(event, tmp, &avplg_event_list, list) {
		avplg_event_done(event);
		avplg_event_rem_nolock(event);
	}

	avplg_event_accept = 0;
	spin_unlock(&avplg_event_lock);
}

struct avplg_event *avplg_event_pop(void)
{
	struct avplg_event *event;

	spin_lock(&avplg_event_lock);

	if (list_empty(&avplg_event_list)) {
		spin_unlock(&avplg_event_lock);
		return NULL;
	}

	event = list_entry(avplg_event_list.next, struct avplg_event, list);

	list_del_init(&event->list);

	spin_unlock(&avplg_event_lock);

	return event;
}

void avplg_event_push(struct avplg_event *event)
{
	spin_lock(&avplg_event_lock);

	if (!avplg_event_accept) {
		spin_unlock(&avplg_event_lock);
		avplg_event_done(event);
		avplg_event_put(event);
		return;
	}

	list_add(&event->list, &avplg_event_list);

	wake_up_interruptible(&avplg_event_available);

	spin_unlock(&avplg_event_lock);
}

int avplg_event_get_file(struct avplg_event *event)
{
	struct file *file;
	int flags;
	int fd;

	flags = O_RDONLY | O_LARGEFILE;

	fd = get_unused_fd();
	if (fd < 0)
		return fd;

	file = dentry_open(&event->path, flags, current_cred());
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return PTR_ERR(file);
	}

	event->fd = fd;
	event->file = file;

	return 0;
}

void avplg_event_put_file(struct avplg_event *event)
{
	if (event->fd > 0)
		put_unused_fd(event->fd);

	if (event->file)
		fput(event->file);
	
	event->fd = -1;
	event->file = NULL;
}

ssize_t avplg_event2buf(char __user *buf, size_t size,
		struct avplg_event *event)
{
	char cmd[256];
	ssize_t len;

	len = snprintf(cmd, 256, "ver:%u,id:%lu,type:%d,fd:%d,pid:%d,tgid:%d",
			AVPLG_PROT_VERSION, event->id, event->type, event->fd,
			event->pid, event->tgid);
	len++;

	if (len > size)
		return -EINVAL;

	if (copy_to_user(buf, cmd, len))
		return -EFAULT;

	return len;
}

struct avplg_event *avplg_buf2event(const char __user *buf, size_t size)
{
	struct avplg_event *event;
	struct avplg_task *task;
	char cmd[256];
	unsigned long id;
	int result;
	int rv;
	int ver;

	if (size > 256)
		return ERR_PTR(-EINVAL);

	if (copy_from_user(cmd, buf, size))
		return ERR_PTR(-EFAULT);

	rv = sscanf(buf, "ver:%u,id:%lu,res:%d", &ver, &id, &result);
	if (rv != 3)
		return ERR_PTR(-EINVAL);

	if (ver != AVPLG_PROT_VERSION)
		return ERR_PTR(-EINVAL);

	task = avplg_task_find(current->tgid);
	if (!task)
		return ERR_PTR(-EINVAL);

	event = avplg_task_pop_event(task, id);
	avplg_task_put(task);

	if (!event)
		return ERR_PTR(-EINVAL);

	event->result = result;

	return event;
}

int avplg_event_empty(void)
{
	int rv;

	spin_lock(&avplg_event_lock);
	rv = list_empty(&avplg_event_list);
	spin_unlock(&avplg_event_lock);

	return rv;
}

int avplg_event_init(void)
{
	avplg_event_cache = kmem_cache_create("avplg_event_cache",
			sizeof(struct avplg_event), 0, 0, NULL);

	if (!avplg_event_cache)
		return -ENOMEM;

	return 0;
}

void avplg_event_exit(void)
{
	kmem_cache_destroy(avplg_event_cache);
}
