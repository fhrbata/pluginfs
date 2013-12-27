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

static int plgfs_remount_fs(struct super_block *sb, int *f, char *d)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct super_block *sbh;
	struct plgfs_mnt_cfg *cfg;
	int rv;

	sbi = plgfs_sbi(sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cfg = plgfs_get_cfg_nodev(*f, d);
	if (IS_ERR(cfg))
		return PTR_ERR(cfg);

	cont->op_id = PLGFS_SOP_REMOUNT_FS;
	cont->op_args.s_remount_fs.sb = sb;
	cont->op_args.s_remount_fs.flags = f;
	cont->op_args.s_remount_fs.data = d;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;
	
	cont->op_rv.rv_int = 0;

	if (!sbi->pdev)
		goto postcalls;

	sb = cont->op_args.s_remount_fs.sb;
	f = cont->op_args.s_remount_fs.flags;
	d = cont->op_args.s_remount_fs.data;

	sbh = plgfs_sbh(sb);
	if (!sbh->s_op->remount_fs)
		goto postcalls;

	cont->op_rv.rv_int = sbh->s_op->remount_fs(sbh, f, cfg->opts);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	plgfs_put_cfg(cfg);

	return rv;
}

static int plgfs_statfs(struct dentry *d, struct kstatfs *buf)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(d->d_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_SOP_STATFS;
	cont->op_args.s_statfs.dentry = d;
	cont->op_args.s_statfs.buf = buf;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;
	
	d = cont->op_args.s_statfs.dentry;
	buf = cont->op_args.s_statfs.buf;

	dh = plgfs_dh(d);

	if (!dh->d_sb->s_op->statfs) {
		cont->op_rv.rv_int = -ENOSYS;
		goto postcalls;
	}

	cont->op_rv.rv_int = dh->d_sb->s_op->statfs(dh, buf);
	if (cont->op_rv.rv_int)
		goto postcalls;

	buf->f_type = PLGFS_MAGIC; 

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static const struct super_operations plgfs_sops = {
	.evict_inode = plgfs_evict_inode,
	.put_super = plgfs_put_super,
	.show_options = plgfs_show_options,
	.remount_fs = plgfs_remount_fs,
	.statfs = plgfs_statfs
};

static const char *plgfs_supported_fs_names[] = {
	"ext2", "ext3", "ext4", "xfs", "vfat", "msdos", NULL
};

static struct vfsmount *plgfs_mount_hidden_known(int flags,
		const char *dev_name, char *fstype, void *data)
{
	struct file_system_type *type;
	struct vfsmount *mnt;

	type = get_fs_type(fstype);
	if (!type)
		return ERR_PTR(-ENODEV);

	mnt = vfs_kern_mount(type, flags | MS_KERNMOUNT, dev_name,
			data);

	module_put(type->owner);

	if (!IS_ERR(mnt))
		return mnt;

	return ERR_PTR(-ENODEV);
}

static struct vfsmount *plgfs_mount_hidden_unknown(int flags,
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

static void plgfs_cp_opts(struct plgfs_context *cont)
{
	char *opts_in;
	char *opts_out;

	if (!(cont->plg->flags & PLGFS_PLG_HAS_OPTS))
		return;

	opts_in = cont->op_args.t_mount.opts_in;
	opts_out = cont->op_args.t_mount.opts_out;

	memcpy(opts_in, opts_out, PAGE_SIZE);
	opts_out[0] = 0;
}

int plgfs_fill_super(struct super_block *sb, int flags,
		struct plgfs_mnt_cfg *cfg)
{
	struct plgfs_sb_info *sbi;
	struct plgfs_context *cont;
	struct dentry *drh; /* dentry root hidden */
	struct inode *ir; /* inode root */
	char path[16];
	int rv;

	sbi = plgfs_alloc_sbi(cfg);
	if (IS_ERR(sbi))
		return PTR_ERR(sbi);

	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont)) {
		plgfs_free_sbi(sbi);
		return PTR_ERR(cont);
	}

	cfg->opts_orig[0] = 0;

	cont->op_id = PLGFS_TOP_MOUNT;
	cont->op_args.t_mount.bdev = cfg->bdev;
	cont->op_args.t_mount.opts_in = cfg->opts;
	cont->op_args.t_mount.opts_out = cfg->opts_orig;

	if (!plgfs_precall_plgs_cb(cont, sbi, plgfs_cp_opts))
		goto postcalls;

	if (cfg->bdev) {
		sbi->pdev = plgfs_add_dev(cfg->bdev, cfg->mode);
		if (IS_ERR(sbi->pdev)) {
			cont->op_rv.rv_int = PTR_ERR(sbi->pdev);
			goto postcalls;
		}

		/* is there any way how to get the blkdev path */
		snprintf(path, 16, "/dev/%s", sbi->pdev->gd->disk_name);

		if (cfg->fstype_str)
			sbi->mnt_hidden = plgfs_mount_hidden_known(flags, path,
					cfg->fstype_str, cfg->opts);
		else
			sbi->mnt_hidden = plgfs_mount_hidden_unknown(flags,
					path, cfg->opts);

		if (IS_ERR(sbi->mnt_hidden)) {
			cont->op_rv.rv_int = PTR_ERR(sbi->mnt_hidden);
			goto postcalls;
		}

		sbi->path_hidden.dentry = sbi->mnt_hidden->mnt_root;
		sbi->path_hidden.mnt = sbi->mnt_hidden;
		path_get(&sbi->path_hidden);

		drh = dget(sbi->mnt_hidden->mnt_root);

	} else {
		sbi->path_hidden = cfg->path;
		path_get(&sbi->path_hidden);
		drh = dget(sbi->path_hidden.dentry);
	}

	cont->op_args.t_mount.path = &sbi->path_hidden;

	sb->s_fs_info = sbi;
	sb->s_magic = PLGFS_MAGIC;
	sb->s_d_op = &plgfs_dops;
	sb->s_op = &plgfs_sops;

	ir = plgfs_iget(sb, (unsigned long)drh->d_inode);
	if (IS_ERR(ir)) {
		cont->op_rv.rv_int = PTR_ERR(ir);
		goto postcalls;
	}

	sb->s_root = d_make_root(ir);
	if (!sb->s_root) {
		cont->op_rv.rv_int = -ENOMEM;
		goto postcalls;
	}

	sb->s_root->d_fsdata = plgfs_alloc_di(sb->s_root);
	if (IS_ERR(sb->s_root->d_fsdata)) {
		cont->op_rv.rv_int = PTR_ERR(sb->s_root->d_fsdata);
		goto postcalls;
	}

	plgfs_di(sb->s_root)->dentry_hidden = drh;

	sb->s_flags |= MS_ACTIVE;

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;
	plgfs_free_context(sbi, cont);

	if (rv) {
		/* generic_shutdown_super does not call put_super unless the
		 * sb->root is set, so in case of error, we call it here
		 * manually. */
		plgfs_free_sbi(sbi);
		sb->s_fs_info = NULL;
	}

	return rv;
}
