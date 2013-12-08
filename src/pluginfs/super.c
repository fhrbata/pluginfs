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

static void plgfs_evict_inode(struct inode *i)
{
	iput(plgfs_ih(i));
	kmem_cache_free(plgfs_sbi(i->i_sb)->cache->ii_cache, plgfs_ii(i));

	if (i->i_data.nrpages)
		truncate_inode_pages(&i->i_data, 0);

	clear_inode(i);
}

static void plgfs_free_sbi(struct plgfs_sb_info *sbi)
{
	if (!sbi)
		return;

	path_put(&sbi->path_hidden);

	if (sbi->mnt_hidden)
		kern_unmount(sbi->mnt_hidden);	

	if (sbi->cache)
		plgfs_cache_put(sbi->cache);

	if (sbi->plgs)
		plgfs_put_plgs(sbi->plgs, sbi->plgs_nr);

	if (sbi->pdev)
		plgfs_rem_dev(sbi->pdev);

	kfree(sbi);
}

static void plgfs_put_super(struct super_block *sb)
{
	plgfs_free_sbi(plgfs_sbi(sb));
}

static const struct super_operations plgfs_sops = {
	.evict_inode = plgfs_evict_inode,
	.put_super = plgfs_put_super,
	.show_options = plgfs_show_options
};

static const char *plgfs_supported_fs_names[] = {
	"ext2", "ext3", "ext4", NULL
};

static struct vfsmount *plgfs_mount_hidden(int flags,
		const char *dev_name, void *data)
{
	struct file_system_type *type;
	struct vfsmount *mnt;
	const char **name;

	for (name = plgfs_supported_fs_names; *name; name++) {

		type = get_fs_type(*name);
		if (!type)
			continue;

		mnt = vfs_kern_mount(type, flags | MS_KERNMOUNT, dev_name,
				data);

		module_put(type->owner);

		if (!IS_ERR(mnt))
			return mnt;
	}

	return ERR_PTR(-ENODEV);
}

static struct plgfs_sb_info *plgfs_alloc_sbi(struct plgfs_mnt_cfg *cfg)
{
	struct plgfs_sb_info *sbi;
	size_t size;
	int i;

	size = sizeof(struct plgfs_sb_info);
	size += sizeof(void *) * cfg->plgs_nr * 2;

	sbi = kzalloc(size, GFP_KERNEL);
	if (!sbi)
		return ERR_PTR(-ENOMEM);

	sbi->cache = plgfs_cache_get(cfg->plgs_nr);
	if (IS_ERR(sbi->cache)) {
		kfree(sbi);
		return ERR_PTR(-ENOMEM);
	}

	sbi->plgs_nr = cfg->plgs_nr;
	sbi->plgs = (struct plgfs_plugin **)sbi->data;
	sbi->priv = sbi->data + sbi->plgs_nr;
	mutex_init(&sbi->mutex_walk);

	memcpy(sbi->plgs, cfg->plgs, sizeof(struct plgfs_plugin *) *
			sbi->plgs_nr);

	for (i = 0; i < sbi->plgs_nr; i++) {
		/* this should never fail since we grabbed all plgs in
		   plgfs_get_cfg */
		BUG_ON(!plgfs_get_plg(sbi->plgs[i]->name));
	}

	return sbi;
}

int plgfs_fill_super(struct super_block *sb, int flags,
		struct plgfs_mnt_cfg *cfg)
{
	struct plgfs_sb_info *sbi;
	struct dentry *drh; /* dentry root hidden */
	struct inode *ir; /* inode root */
	char path[16];
	int rv;

	sbi = plgfs_alloc_sbi(cfg);
	if (IS_ERR(sbi))
		return PTR_ERR(sbi);

	if (cfg->bdev) {
		sbi->pdev = plgfs_add_dev(cfg->bdev, cfg->mode);
		rv = PTR_ERR(sbi->pdev);
		if (IS_ERR(sbi->pdev))
			goto err;

		/* is there any way how to get the blkdev path */
		snprintf(path, 16, "/dev/%s", sbi->pdev->gd->disk_name);

		sbi->mnt_hidden = plgfs_mount_hidden(flags, path, cfg->opts);
		rv = PTR_ERR(sbi->mnt_hidden);
		if (IS_ERR(sbi->mnt_hidden))
			goto err;

		sbi->path_hidden.dentry = sbi->mnt_hidden->mnt_root;
		sbi->path_hidden.mnt = sbi->mnt_hidden;
		path_get(&sbi->path_hidden);

		drh = dget(sbi->mnt_hidden->mnt_root);

	} else {
		sbi->path_hidden = cfg->path;
		path_get(&sbi->path_hidden);
		drh = dget(sbi->path_hidden.dentry);
	}

	sb->s_fs_info = sbi;
	sb->s_magic = PLGFS_MAGIC;
	sb->s_d_op = &plgfs_dops;
	sb->s_op = &plgfs_sops;

	/* generic_shutdown_super does not call put_super unless the sb->root
	   is set, so in case of error, we call it here manually till the
	   sb->root is set */

	ir = plgfs_iget(sb, (unsigned long)drh->d_inode);
	rv = PTR_ERR(ir);
	if (IS_ERR(ir))
		goto err;

	rv = -ENOMEM;
	sb->s_root = d_make_root(ir);
	if (!sb->s_root)
		goto err;

	sb->s_root->d_fsdata = plgfs_alloc_di(sb->s_root, drh);
	if (IS_ERR(sb->s_root->d_fsdata))
		return PTR_ERR(sb->s_root->d_fsdata);

	sb->s_flags |= MS_ACTIVE;

	return 0;
err:
	plgfs_free_sbi(sbi);
	return rv;
}

