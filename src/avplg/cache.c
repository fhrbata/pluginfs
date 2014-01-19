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

unsigned long avplg_sb_cache_ver(struct avplg_sb_info *sbi)
{
	rmb();
	return sbi->cache_ver;
}

void avplg_sb_cache_inv(struct avplg_sb_info *sbi)
{
	sbi->cache_ver++;
	wmb();
}

void avplg_icache_update(struct avplg_event *event)
{
	struct avplg_sb_info *sbi;
	struct avplg_inode_info *ii;
	struct inode *i;

	i = event->path.dentry->d_inode;
	ii = avplg_ii(i, event->plg_id);
	sbi = avplg_sbi(i->i_sb, event->plg_id);

	spin_lock(&ii->lock);
	ii->result_ver = event->result_ver;
	ii->cache_sb_ver = avplg_sb_cache_ver(sbi);
	if (event->result > 0)
		ii->result = event->result;
	else
		ii->result = 0;

	spin_unlock(&ii->lock);
}

void avplg_icache_inv(struct file *file, int id)
{
	struct avplg_inode_info *ii;

	ii = avplg_ii(file->f_dentry->d_inode, id);
	spin_lock(&ii->lock);
	ii->cache_ver++;
	spin_unlock(&ii->lock);
}

int avplg_icache_check(struct file *file, int id)
{
	struct avplg_sb_info *sbi;
	struct avplg_inode_info *ii;
	int rv;

	rv = 0;
	ii = avplg_ii(file->f_dentry->d_inode, id);
	sbi = avplg_sbi(file->f_dentry->d_sb, id);

	spin_lock(&ii->lock);

	if (ii->result_ver != ii->cache_ver)
		goto exit;

	if (ii->cache_sb_ver != avplg_sb_cache_ver(sbi))
		goto exit;

	rv = ii->result;
exit:
	spin_unlock(&ii->lock);

	return rv;
}
