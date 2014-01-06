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

static struct class *avplg_class;
static struct device *avplg_device;
static dev_t avplg_devt;

static int avplg_chrdev_open_task(struct inode *inode, struct file *file)
{
	int rv;

	rv = avplg_task_add(current->tgid);
	if (rv)
		return rv;

	avplg_event_start();

	return 0;
}

static int avplg_chrdev_open_trusted(struct inode *inode, struct file *file)
{
	return avplg_trusted_add(current->tgid);
}

static int avplg_chrdev_open(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_WRITE)
		return avplg_chrdev_open_task(inode, file);

	return avplg_chrdev_open_trusted(inode, file);
}

static int avplg_chrdev_release_task(struct inode *inode, struct file *file)
{
	int rv;

	rv = avplg_task_rem(current->tgid);
	if (rv)
		return rv;

	if (!avplg_task_empty())
		return 0;

	avplg_event_stop();

	return 0;
}

static int avplg_chrdev_release_trusted(struct inode *inode, struct file *file)
{
	return avplg_trusted_rem(current->tgid);
}

static int avplg_chrdev_release(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_WRITE)
		return avplg_chrdev_release_task(inode, file);

	return avplg_chrdev_release_trusted(inode, file);
}

static ssize_t avplg_chrdev_read(struct file *file, char __user *buf,
		size_t size, loff_t *pos)
{
	struct avplg_event *event;
	ssize_t len;
	ssize_t rv;

	if (!(file->f_mode & FMODE_WRITE))
		return -EINVAL;

	event = avplg_event_pop();
	if (!event)
		return 0;

	rv = avplg_event_get_file(event);
	if (rv)
		goto error;

	rv = len = avplg_event2buf(buf, size, event);
	if (rv < 0)
		goto error;

	rv = avplg_task_add_event(event);
	if (rv)
		goto error;

	fd_install(event->fd, event->file);
	avplg_event_put(event);
	return len;
error:
	avplg_event_put_file(event);
	avplg_task_rem_event(event);
	avplg_event_push(event);
	return rv;
}

static ssize_t avplg_chrdev_write(struct file *file, const char __user *buf,
		size_t size, loff_t *pos)
{
	struct avplg_event *event;

	event = avplg_buf2event(buf, size);
	if (IS_ERR(event))
		return PTR_ERR(event);

	avplg_event_done(event);
	avplg_event_put(event);

	return size;
}

static unsigned int avplg_chrdev_poll(struct file *file, poll_table *wait)
{
	unsigned int mask;

	poll_wait(file, &avplg_event_available, wait);

	mask = POLLOUT | POLLWRNORM;

	if (!avplg_event_empty())
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static struct file_operations avplg_chrdev_fops = {
	.owner = THIS_MODULE,
	.open = avplg_chrdev_open,
	.release = avplg_chrdev_release,
	.read = avplg_chrdev_read,
	.write = avplg_chrdev_write,
	.poll = avplg_chrdev_poll,
};

int avplg_chrdev_init(void)
{
	int major;

	major = register_chrdev(0, "avplg", &avplg_chrdev_fops);
	if (major < 0)
		return major;

	avplg_devt = MKDEV(major, 0);

	avplg_class = class_create(THIS_MODULE, "avplg");
	if (IS_ERR(avplg_class)) {
		unregister_chrdev(major, "avplg");
		return PTR_ERR(avplg_class);
	}

	avplg_device = device_create(avplg_class, NULL, avplg_devt, NULL,
			"avplg");
	if (IS_ERR(avplg_device)) {
		class_destroy(avplg_class);
		unregister_chrdev(major, "avplg");
		return PTR_ERR(avplg_device);
	}

	return 0;
}

void avplg_chrdev_exit(void)
{
	device_destroy(avplg_class, avplg_devt);
	class_destroy(avplg_class);
	unregister_chrdev(MAJOR(avplg_devt), "avplg");
}
