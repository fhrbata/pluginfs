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

static struct avplg_path *avplg_path_alloc(struct dentry *d)
{
	struct avplg_path *path;

	path = kzalloc(sizeof(struct avplg_path), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&path->list);
	path->dentry = dget(d);

	return path;
}

void avplg_path_free(struct avplg_path *path)
{
	dput(path->dentry);
	kfree(path);
}

static int avplg_set_path_cb(struct dentry *d, void *data, int id)
{
	struct avplg_inode_info *ii;
	int info;

	info = (unsigned long)data;

	if (!d->d_inode)
		return 1; /* skip */

	ii = avplg_ii(d->d_inode, id);

	if (atomic_read(&ii->path_info) & AVPLG_I_PATH)
		return 1;

	atomic_set(&ii->path_info, info);

	return 0;
}

int avplg_add_path(struct avplg_sb_info *sbi, struct dentry *d, int id,
		int incl)
{
	struct avplg_inode_info *ii;
	struct avplg_path *path;
	int info;

	if (!d->d_inode)
		return -EINVAL;

	ii = avplg_ii(d->d_inode, id);

	if (atomic_read(&ii->path_info) & AVPLG_I_PATH)
		return -EEXIST;

	path = avplg_path_alloc(d);
	if (IS_ERR(path))
		return PTR_ERR(path);

	if (incl)
		info = AVPLG_I_INCL;
	else
		info = AVPLG_I_EXCL;

	list_add_tail(&path->list, &sbi->paths);

	plgfs_walk_dtree(&avplg, d, avplg_set_path_cb,
			(void *)(unsigned long)info);

	atomic_set(&ii->path_info, info | AVPLG_I_PATH);

	return 0;
}

static struct avplg_path *avplg_find_path(struct avplg_sb_info *sbi,
		struct dentry *d)
{
	struct avplg_path *path;

	list_for_each_entry(path, &sbi->paths, list) {
		if (path->dentry == d)
			return path;
	}

	return NULL;
}

int avplg_rem_path(struct avplg_sb_info *sbi, struct dentry *d, int id)
{
	struct avplg_path *path;
	struct avplg_inode_info *ii;
	int info;

	if (!d->d_inode)
		return -EINVAL;

	path = avplg_find_path(sbi, d);
	if (!path)
		return -EINVAL;

	ii = avplg_ii(d->d_inode, id);
	if (!(atomic_read(&ii->path_info) & AVPLG_I_PATH))
		return -EINVAL;

	if (IS_ROOT(d)) {
		info = AVPLG_I_NONE;
	} else {
		ii = avplg_ii(d->d_parent->d_inode, id);
		info = atomic_read(&ii->path_info) & ~AVPLG_I_PATH;
	}

	atomic_set(&ii->path_info, info);

	plgfs_walk_dtree(&avplg, d, avplg_set_path_cb,
			(void *)(unsigned long)info);
	list_del_init(&path->list);
	avplg_path_free(path);

	return 0;
}

void avplg_set_paths(struct super_block *sb, char *str, int id, int incl)
{
	struct avplg_sb_info *sbi;
	struct dentry *d;
	struct dentry *root;
	char *path;
	int rv;

	if (!str)
		return;

	sbi = avplg_sbi(sb, id);
	root = sb->s_root;

	while ((path = strsep(&str, ":")) != NULL) {
		if (!*path)
			continue;

		d = plgfs_dentry_lookup(root, path);
		if (IS_ERR(d)) {
			pr_err("avplg: %s not found\n", path);
			continue;
		}

		if (!d->d_inode) {
			pr_err("avplg: %s negative dentry\n", path);
			goto next;
		}

		rv = avplg_add_path(sbi, d, id, incl);
		if (rv) {
			pr_err("avplg: %s cannot add %d\n", path, rv);
			goto next;
		}
next:
		dput(d);
	}
}

void avplg_rem_paths(struct super_block *sb, int id)
{
	struct avplg_sb_info *sbi;
	struct avplg_path *path;
	struct avplg_path *tmp;
	struct avplg_inode_info *ii;

	sbi = avplg_sbi(sb, id);

	list_for_each_entry_safe(path, tmp, &sbi->paths, list) {
		ii = avplg_ii(path->dentry->d_inode, id);
		atomic_set(&ii->path_info, AVPLG_I_NONE);
		list_del_init(&path->list);
		avplg_path_free(path);
	}

	plgfs_walk_dtree(&avplg, sb->s_root, avplg_set_path_cb,
			(void *)(unsigned long)AVPLG_I_NONE);
}

void avplg_show_paths(struct super_block *sb, int id, struct seq_file *seq)
{
	struct avplg_sb_info *sbi;
	struct avplg_inode_info *ii;
	struct avplg_path *path;
	int cnt;
	char *buf;
	char *fn;

	buf = (char *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!buf)
		return;

	sbi = avplg_sbi(sb, id);

	cnt = 0;
	list_for_each_entry(path, &sbi->paths, list) {
		ii = avplg_ii(path->dentry->d_inode, id);
		if (!(atomic_read(&ii->path_info) & AVPLG_I_INCL))
			continue;

		fn = plgfs_dpath(path->dentry, buf, PAGE_SIZE);
		if (IS_ERR(fn))
			continue;

		if (!cnt)
			seq_printf(seq, ",avplg_incl=%s", fn);
		else
			seq_printf(seq, ":%s", fn);

		cnt++;
	}

	cnt = 0;
	list_for_each_entry(path, &sbi->paths, list) {
		ii = avplg_ii(path->dentry->d_inode, id);
		if (!(atomic_read(&ii->path_info) & AVPLG_I_EXCL))
			continue;

		fn = plgfs_dpath(path->dentry, buf, PAGE_SIZE);
		if (IS_ERR(fn))
			continue;

		if (!cnt)
			seq_printf(seq, ",avplg_excl=%s", fn);
		else
			seq_printf(seq, ":%s", fn);

		cnt++;
	}

	free_page((unsigned long)buf);
}
