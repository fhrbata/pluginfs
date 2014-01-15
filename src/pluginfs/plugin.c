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

#include "plgfs.h"

static DEFINE_MUTEX(plgfs_plg_list_mutex);
static LIST_HEAD(plgfs_plg_list);

int plgfs_get_plugin_sb_id(struct plgfs_plugin *plg, struct super_block *sb)
{
	struct plgfs_sb_info *sbi;
	int i;

	if (IS_ERR_OR_NULL(plg) || IS_ERR_OR_NULL(sb))
		return -EINVAL;

	if (sb->s_magic != PLGFS_MAGIC)
		return -EINVAL;

	sbi = plgfs_sbi(sb);

	for (i = 0; i < sbi->plgs_nr; i++)
	{
		if (sbi->plgs[i] == plg)
			return i;
	}

	return -ENOENT;
}

static struct plgfs_plugin *plgfs_find_plg(const char *name, int prio)
{
	struct plgfs_plugin *plg;

	list_for_each_entry(plg, &plgfs_plg_list, list) {
		
		if (strcmp(plg->name, name))
			continue;

		if (!prio)
			return plg;

		if (plg->priority != prio)
			continue;

		return plg;
	}

	return NULL;
}

int plgfs_register_plugin(struct plgfs_plugin *plg)
{
	if (IS_ERR_OR_NULL(plg) || !plg->name || !plg->owner)
		return -EINVAL;

	if (plg->priority < 0)
		return -EINVAL;

	mutex_lock(&plgfs_plg_list_mutex);

	if (plgfs_find_plg(plg->name, plg->priority)) {
		mutex_unlock(&plgfs_plg_list_mutex);
		return -EEXIST;
	}

	INIT_LIST_HEAD(&plg->list);
	list_add(&plg->list, &plgfs_plg_list);

	mutex_unlock(&plgfs_plg_list_mutex);

	return 0;
}

int plgfs_unregister_plugin(struct plgfs_plugin *plg)
{
	if (IS_ERR_OR_NULL(plg))
		return -EINVAL;

	mutex_lock(&plgfs_plg_list_mutex);
	if (!plgfs_find_plg(plg->name, plg->priority)) {
		mutex_unlock(&plgfs_plg_list_mutex);
		return -EINVAL;
	}

	list_del_init(&plg->list);
	mutex_unlock(&plgfs_plg_list_mutex);

	return 0;
}

struct plgfs_plugin *plgfs_get_plg(const char *name)
{
	struct plgfs_plugin *plg;

	mutex_lock(&plgfs_plg_list_mutex);

	plg = plgfs_find_plg(name, 0);
	if (plg)
		goto exit;

	mutex_unlock(&plgfs_plg_list_mutex);

	if (request_module(name))
		return NULL;

	mutex_lock(&plgfs_plg_list_mutex);

	plg = plgfs_find_plg(name, 0);
exit:
	if (!try_module_get(plg->owner))
		plg = NULL;

	mutex_unlock(&plgfs_plg_list_mutex);

	return plg;
}

inline void plgfs_put_plg(struct plgfs_plugin *plg)
{
	module_put(plg->owner);
}

void plgfs_put_plgs(struct plgfs_plugin **plgs, int nr)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (!plgs[i])
			continue;
		plgfs_put_plg(plgs[i]);
	}
}

void **plgfs_sb_priv(struct super_block *sb, int plg_sb_id)
{
	return &plgfs_sbi(sb)->priv[plg_sb_id];
}

void **plgfs_file_priv(struct file *f, int plg_sb_id)
{
	return &plgfs_fi(f)->priv[plg_sb_id];
}

void **plgfs_dentry_priv(struct dentry *d, int plg_sb_id)
{
	return &plgfs_di(d)->priv[plg_sb_id];
}

void **plgfs_inode_priv(struct inode *i, int plg_sb_id)
{
	return &plgfs_ii(i)->priv[plg_sb_id];
}

EXPORT_SYMBOL(plgfs_register_plugin);
EXPORT_SYMBOL(plgfs_unregister_plugin);
EXPORT_SYMBOL(plgfs_walk_dtree);
EXPORT_SYMBOL(plgfs_get_plugin_sb_id);
EXPORT_SYMBOL(plgfs_sb_priv);
EXPORT_SYMBOL(plgfs_file_priv);
EXPORT_SYMBOL(plgfs_dentry_priv);
EXPORT_SYMBOL(plgfs_inode_priv);
