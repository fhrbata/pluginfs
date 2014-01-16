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

static atomic_t avplg_cache_glob_cnt = ATOMIC_INIT(0);

unsigned long avplg_cache_glob_ver(void)
{
	return atomic_read(&avplg_cache_glob_cnt);
}

void avplg_cache_glob_inc(void)
{
	atomic_inc(&avplg_cache_glob_cnt);
}

void avplg_cache_update(struct avplg_event *event)
{
	struct avplg_inode_info *ii;

	ii = avplg_ii(event->path.dentry->d_inode, event->plg_id);

	spin_lock(&ii->lock);
	ii->result_ver = event->result_ver;
	ii->cache_glob_ver = event->cache_glob_ver;
	if (event->result > 0)
		ii->result = event->result;
	else
		ii->result = 0;

	spin_unlock(&ii->lock);
}

void avplg_cache_inc(struct file *file, int id)
{
	struct avplg_inode_info *ii;

	ii = avplg_ii(file->f_dentry->d_inode, id);
	spin_lock(&ii->lock);
	ii->cache_ver++;
	spin_unlock(&ii->lock);
}

int avplg_cache_check(struct file *file, int id)
{
	struct avplg_inode_info *ii;
	int rv;

	rv = 0;
	ii = avplg_ii(file->f_dentry->d_inode, id);

	spin_lock(&ii->lock);

	if (ii->result_ver != ii->cache_ver)
		goto exit;

	if (ii->cache_glob_ver != avplg_cache_glob_ver())
		goto exit;

	rv = ii->result;
exit:
	spin_unlock(&ii->lock);

	return rv;
}
