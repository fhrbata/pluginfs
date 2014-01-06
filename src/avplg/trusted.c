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

static LIST_HEAD(avplg_trusted_list);
static DEFINE_SPINLOCK(avplg_trusted_lock);

static struct avplg_trusted *avplg_trusted_alloc(pid_t tgid)
{
	struct avplg_trusted *trusted;

	trusted = kzalloc(sizeof(struct avplg_trusted), GFP_KERNEL);
	if (!trusted)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&trusted->list);
	trusted->tgid = tgid;
	trusted->opened = 1;

	return trusted;
}

static void avplg_trusted_free(struct avplg_trusted *trusted)
{
	kfree(trusted);
}

static struct avplg_trusted *avplg_trusted_find(pid_t tgid)
{
	struct avplg_trusted *trusted;

	list_for_each_entry(trusted, &avplg_trusted_list, list) {
		if (trusted->tgid == tgid)
			return trusted;
	}

	return NULL;
}

int avplg_trusted_add(pid_t tgid)
{
	struct avplg_trusted *trusted;
	struct avplg_trusted *found;

	trusted = avplg_trusted_alloc(tgid);
	if (IS_ERR(trusted))
		return PTR_ERR(trusted);

	spin_lock(&avplg_trusted_lock);

	found = avplg_trusted_find(tgid);
	if (found) {
		found->opened++;
		avplg_trusted_free(trusted); 
	} else 
		list_add_tail(&trusted->list, &avplg_trusted_list);

	spin_unlock(&avplg_trusted_lock);

	return 0;
}

int avplg_trusted_rem(pid_t tgid)
{
	struct avplg_trusted *found;

	spin_lock(&avplg_trusted_lock);

	found = avplg_trusted_find(tgid);
	if (!found) {
		spin_unlock(&avplg_trusted_lock);
		return -EINVAL;
	}

	if (--found->opened) {
		spin_unlock(&avplg_trusted_lock);
		return 0;
	}

	list_del_init(&found->list);

	spin_unlock(&avplg_trusted_lock);

	avplg_trusted_free(found);

	return 0;
}

int avplg_trusted_allow(pid_t tgid)
{
	struct avplg_trusted *found;

	spin_lock(&avplg_trusted_lock);
	found = avplg_trusted_find(tgid);
	spin_unlock(&avplg_trusted_lock);

	if (found)
		return 1;

	return 0;
}
