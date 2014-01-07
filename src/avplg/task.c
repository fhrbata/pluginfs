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

static LIST_HEAD(avplg_task_list);
static DEFINE_SPINLOCK(avplg_task_lock);

static struct avplg_task *avplg_task_alloc(pid_t tgid)
{
	struct avplg_task *task;

	task = kzalloc(sizeof(struct avplg_task), GFP_KERNEL);
	if (!task)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&task->list);
	INIT_LIST_HEAD(&task->events);
	spin_lock_init(&task->lock);
	atomic_set(&task->count, 1);
	task->tgid = tgid;
	task->opened = 1;

	return task;
}

struct avplg_task *avplg_task_get(struct avplg_task *task)
{
	if (!task || IS_ERR(task))
		return NULL;

	BUG_ON(!atomic_read(&task->count));
	atomic_inc(&task->count);
	return task;
}

void avplg_task_put(struct avplg_task *task)
{
	struct avplg_event *event;
	struct avplg_event *tmp;

	if (!task || IS_ERR(task))
		return;

	BUG_ON(!atomic_read(&task->count));

	if (!atomic_dec_and_test(&task->count))
		return;

	list_for_each_entry_safe(event, tmp, &task->events, task_event) {
		list_del_init(&event->task_event);
		avplg_event_push(event);
	}

	kfree(task);
}

static struct avplg_task *avplg_task_find_nolock(pid_t tgid)
{
	struct avplg_task *task;

	list_for_each_entry(task, &avplg_task_list, list) {
		if (task->tgid == tgid)
			return avplg_task_get(task);
	}

	return NULL;
}

struct avplg_task *avplg_task_find(pid_t tgid)
{
	struct avplg_task *task;

	spin_lock(&avplg_task_lock);
	task = avplg_task_find_nolock(tgid);
	spin_unlock(&avplg_task_lock);

	return task;
}

int avplg_task_add(pid_t tgid)
{
	struct avplg_task *task;
	struct avplg_task *found;

	task = avplg_task_alloc(tgid);
	if (IS_ERR(task))
		return PTR_ERR(task);

	spin_lock(&avplg_task_lock);

	found = avplg_task_find_nolock(tgid);
	if (found) {
		found->opened++;
		avplg_task_put(task); 
	} else 
		list_add_tail(&task->list, &avplg_task_list);

	spin_unlock(&avplg_task_lock);

	return 0;
}

int avplg_task_rem(pid_t tgid)
{
	struct avplg_task *found;

	spin_lock(&avplg_task_lock);

	found = avplg_task_find(tgid);
	if (!found) {
		spin_unlock(&avplg_task_lock);
		return -EINVAL;
	}

	if (--found->opened) {
		spin_unlock(&avplg_task_lock);
		return 0;
	}

	list_del_init(&found->list);

	spin_unlock(&avplg_task_lock);

	avplg_task_put(found);
	avplg_task_put(found);

	return 0;
}

int avplg_task_allow(pid_t tgid)
{
	struct avplg_task *found;

	found = avplg_task_find(tgid);

	if (!found)
		return 0;

	avplg_task_put(found);

	return 1;
}

int avplg_task_empty(void)
{
	int empty;

	spin_lock(&avplg_task_lock);
	empty = list_empty(&avplg_task_list);
	spin_unlock(&avplg_task_lock);

	return empty;
}

int avplg_task_add_event(struct avplg_event *event)
{
	struct avplg_task *task;

	task = avplg_task_find(current->tgid);
	if (!task)
		return -EINVAL;

	spin_lock(&task->lock);

	list_add_tail(&event->task_event, &task->events);

	avplg_event_get(event);

	event->id = task->event_ids++;

	spin_unlock(&task->lock);

	avplg_task_put(task);

	return 0;
}

int avplg_task_rem_event(struct avplg_event *event)
{
	struct avplg_task *task;

	task = avplg_task_find(current->tgid);
	if (!task)
		return -EINVAL;

	spin_lock(&task->lock);
	if (list_empty(&event->task_event)) {
		spin_unlock(&task->lock);
		avplg_task_put(task);
		return 0;
	}

	list_del_init(&event->task_event);

	avplg_event_put(event);

	event->id = 0;

	spin_unlock(&task->lock);

	avplg_task_put(task);

	return 0;
}

struct avplg_event *avplg_task_pop_event(struct avplg_task *task,
		unsigned long id)
{
	struct avplg_event *event;
	struct avplg_event *found;

	found = NULL;

	spin_lock(&task->lock);

	list_for_each_entry(event, &task->events, task_event) {
		if (event->id == id) {
			found = event;
			break;
		}
	}

	if (found)
		list_del_init(&event->task_event);

	spin_unlock(&task->lock);

	return found;
}
