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

static struct dentry *plgfs_dir_iop_lookup(struct inode *i, struct dentry *d,
		unsigned int flags)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dph; /* dentry parent hidden */
	struct dentry *dh; /* dentry hidden */
	struct dentry *rv;

	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return ERR_CAST(cont);

	cont->op_id = PLGFS_DIR_IOP_LOOKUP;
	cont->op_args.i_lookup.dir = i;
	cont->op_args.i_lookup.dentry = d;
	cont->op_args.i_lookup.flags = flags;

	if(!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	i = cont->op_args.i_lookup.dir;
	d = cont->op_args.i_lookup.dentry;
	flags = cont->op_args.i_lookup.flags;

	dph = plgfs_dh(d->d_parent);

	d->d_fsdata = plgfs_alloc_di(d);
	if (IS_ERR(d->d_fsdata)) {
		cont->op_rv.rv_dentry = ERR_CAST(d->d_fsdata);
		goto postcalls;
	}

	mutex_lock(&dph->d_inode->i_mutex);
	dh = lookup_one_len(d->d_name.name, dph, d->d_name.len);
	mutex_unlock(&dph->d_inode->i_mutex);

	if (IS_ERR(dh)) {
		cont->op_rv.rv_dentry = dh;
		goto postcalls;
	}

	plgfs_di(d)->dentry_hidden = dh;

	if (!dh->d_inode) {
		d_add(d, NULL);
		goto postcalls;
	}

	i = plgfs_iget(i->i_sb, (unsigned long)dh->d_inode);
	/* dput of our dentry will also free the hidden one */
	if (IS_ERR(i)) {
		cont->op_rv.rv_dentry = ERR_CAST(i);
		goto postcalls;
	}

	d_add(d, i);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_dentry;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_create(struct inode *ip, struct dentry *d,
		umode_t mode, bool excl)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *iph; /* dentry parent hidden */
	struct dentry *dh; /* dentry hidden */
	struct inode *i;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_CREATE;
	cont->op_args.i_create.dir = ip;
	cont->op_args.i_create.dentry = d;
	cont->op_args.i_create.mode = mode;
	cont->op_args.i_create.excl = excl;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_create.dir;
	d = cont->op_args.i_create.dentry;
	mode = cont->op_args.i_create.mode;
	excl = cont->op_args.i_create.excl;

	iph = plgfs_ih(ip);
	dh = plgfs_dh(d);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_create(iph, dh, mode, excl);
	mutex_unlock(&iph->i_mutex);
	if (cont->op_rv.rv_int)
		goto postcalls;

	i = plgfs_iget(ip->i_sb, (unsigned long)dh->d_inode);
	if (IS_ERR(i)) {
		mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
		rv = vfs_unlink(iph, dh);
		mutex_unlock(&iph->i_mutex);
		if (rv)
			pr_err("pluginfs: create: unlink failed: %d\n", rv);

		cont->op_rv.rv_int = PTR_ERR(i);
		goto postcalls;
	}

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_inode_size(ip, iph);
	d_instantiate(d, i);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_iop_setattr(struct dentry *d, struct iattr *ia, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dh;
	struct file *f;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_setattr.dentry = d;
	cont->op_args.i_setattr.iattr = ia;

	f = ia->ia_file;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.i_setattr.dentry;
	ia = cont->op_args.i_setattr.iattr;
	f = ia->ia_file;

	if (ia->ia_valid & ATTR_FILE)
		ia->ia_file = plgfs_fh(f);

	if (ia->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		ia->ia_valid &= ~ATTR_MODE;

	d = cont->op_args.i_setattr.dentry;
	ia = cont->op_args.i_setattr.iattr;
	dh = plgfs_dh(d);

	mutex_lock(&dh->d_inode->i_mutex);
	cont->op_rv.rv_int = notify_change(dh, ia);
	mutex_unlock(&dh->d_inode->i_mutex);

	fsstack_copy_attr_all(d->d_inode, dh->d_inode);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	ia->ia_file = f;

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_iop_setattr(struct dentry *d, struct iattr *ia)
{
	return plgfs_iop_setattr(d, ia, PLGFS_REG_IOP_SETATTR);
}

static int plgfs_dir_iop_setattr(struct dentry *d, struct iattr *ia)
{
	return plgfs_iop_setattr(d, ia, PLGFS_DIR_IOP_SETATTR);
}

static int plgfs_lnk_iop_setattr(struct dentry *d, struct iattr *ia)
{
	return plgfs_iop_setattr(d, ia, PLGFS_LNK_IOP_SETATTR);
}

static int plgfs_iop_getattr(struct vfsmount *m, struct dentry *d,
		struct kstat *stat, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct path path;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_getattr.mnt = m;
	cont->op_args.i_getattr.dentry = d;
	cont->op_args.i_getattr.stat = stat;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	m = cont->op_args.i_getattr.mnt;
	d = cont->op_args.i_getattr.dentry;
	stat = cont->op_args.i_getattr.stat;

	path.mnt = sbi->path_hidden.mnt;
	path.dentry = plgfs_dh(d);

	cont->op_rv.rv_int = vfs_getattr(&path, stat);
	if (cont->op_rv.rv_int)
		goto postcalls;

	fsstack_copy_attr_all(d->d_inode, plgfs_dh(d)->d_inode);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_iop_getattr(struct vfsmount *m, struct dentry *d,
		struct kstat *stat)
{
	return plgfs_iop_getattr(m, d, stat, PLGFS_REG_IOP_GETATTR);
}

static int plgfs_dir_iop_getattr(struct vfsmount *m, struct dentry *d,
		struct kstat *stat)
{
	return plgfs_iop_getattr(m, d, stat, PLGFS_DIR_IOP_GETATTR);
}

static int plgfs_lnk_iop_getattr(struct vfsmount *m, struct dentry *d,
		struct kstat *stat)
{
	return plgfs_iop_getattr(m, d, stat, PLGFS_LNK_IOP_GETATTR);
}

static int plgfs_dir_iop_unlink(struct inode *ip, struct dentry *d)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *ih;
	struct inode *iph;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_UNLINK;
	cont->op_args.i_unlink.dir = ip;
	cont->op_args.i_unlink.dentry = d;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_unlink.dir;
	d = cont->op_args.i_unlink.dentry;
	iph = plgfs_ih(ip);
	ih = plgfs_dh(d)->d_inode;

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_unlink(iph, plgfs_dh(d));
	mutex_unlock(&iph->i_mutex);

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_attr_times(d->d_inode, ih);
	set_nlink(d->d_inode, ih->i_nlink);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_mkdir(struct inode *ip, struct dentry *d, umode_t m)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *iph;
	struct inode *i;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_MKDIR;
	cont->op_args.i_mkdir.dir = ip;
	cont->op_args.i_mkdir.dentry = d;
	cont->op_args.i_mkdir.mode = m;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_mkdir.dir;
	d = cont->op_args.i_mkdir.dentry;
	m = cont->op_args.i_mkdir.mode;
	iph = plgfs_ih(ip);
	dh = plgfs_dh(d);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_mkdir(iph, dh, m);
	mutex_unlock(&iph->i_mutex);

	if (cont->op_rv.rv_int)
		goto postcalls;

	i = plgfs_iget(ip->i_sb, (unsigned long)dh->d_inode);
	if (IS_ERR(i)) {
		mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
		rv = vfs_unlink(iph, dh);
		mutex_unlock(&iph->i_mutex);
		if (rv)
			pr_err("pluginfs: mkdir: unlink failed: %d\n", rv);

		cont->op_rv.rv_int = PTR_ERR(i);
		goto postcalls;
	}

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_inode_size(ip, iph);
	set_nlink(ip, iph->i_nlink);
	d_instantiate(d, i);
postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_rmdir(struct inode *ip, struct dentry *d)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *iph;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_RMDIR;
	cont->op_args.i_mkdir.dir = ip;
	cont->op_args.i_mkdir.dentry = d;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_mkdir.dir;
	d = cont->op_args.i_mkdir.dentry;
	iph = plgfs_ih(ip);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_rmdir(plgfs_ih(ip), plgfs_dh(d));
	mutex_unlock(&iph->i_mutex);

	fsstack_copy_attr_times(ip, iph);
	set_nlink(ip, iph->i_nlink);
postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_rename(struct inode *oi, struct dentry *od,
		struct inode *ni, struct dentry *nd)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *oih;
	struct dentry *odh;
	struct inode *nih;
	struct dentry *ndh;
	struct dentry *trap;
	int rv;

	sbi = plgfs_sbi(oi->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_RENAME;
	cont->op_args.i_rename.old_dir = oi;
	cont->op_args.i_rename.old_dentry = od;
	cont->op_args.i_rename.new_dir = ni;
	cont->op_args.i_rename.new_dentry = nd;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	oi = cont->op_args.i_rename.old_dir;
	od = cont->op_args.i_rename.old_dentry;
	ni = cont->op_args.i_rename.new_dir;
	nd = cont->op_args.i_rename.new_dentry;

	oih = plgfs_ih(oi);
	odh = plgfs_dh(od);
	nih = plgfs_ih(ni);
	ndh = plgfs_dh(nd);

	trap = lock_rename(ndh->d_parent, odh->d_parent);

	if (trap == odh) {
		cont->op_rv.rv_int = -EINVAL;
		goto unlock;
	}

	if (trap == ndh) {
		cont->op_rv.rv_int = -ENOTEMPTY;
		goto unlock;
	}

	cont->op_rv.rv_int = vfs_rename(oih, odh, nih, ndh);
	if (cont->op_rv.rv_int)
		goto unlock;

	fsstack_copy_attr_all(od->d_inode, odh->d_inode);
	fsstack_copy_attr_all(ni, plgfs_ih(ni));
	fsstack_copy_attr_all(oi, plgfs_ih(oi));
unlock:
	unlock_rename(ndh->d_parent, odh->d_parent); 
postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_symlink(struct inode *ip, struct dentry *d,
		const char *n)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *i;
	struct inode *iph;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_SYMLINK;
	cont->op_args.i_symlink.dir = ip;
	cont->op_args.i_symlink.dentry = d;
	cont->op_args.i_symlink.name = n;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_symlink.dir;
	d = cont->op_args.i_symlink.dentry;
	n = cont->op_args.i_symlink.name;

	iph = plgfs_ih(ip);
	dh = plgfs_dh(d);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_symlink(iph, dh, n);
	mutex_unlock(&iph->i_mutex);

	if (cont->op_rv.rv_int)
		goto postcalls;

	i = plgfs_iget(ip->i_sb, (unsigned long)dh->d_inode);
	if (IS_ERR(i)) {
		mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
		rv = vfs_unlink(iph, dh);
		mutex_unlock(&iph->i_mutex);
		if (rv)
			pr_err("pluginfs: symlink: unlink failed: %d\n", rv);

		cont->op_rv.rv_int = PTR_ERR(i);
		goto postcalls;
	}

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_inode_size(ip, iph);
	d_instantiate(d, i);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_lnk_iop_readlink(struct dentry *d, char __user *b, int s)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_LNK_IOP_READLINK;
	cont->op_args.i_readlink.dentry = d;
	cont->op_args.i_readlink.buffer = b;
	cont->op_args.i_readlink.buflen = s;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.i_readlink.dentry;
	b = cont->op_args.i_readlink.buffer;
	s = cont->op_args.i_readlink.buflen;

	cont->op_rv.rv_int = generic_readlink(d, b, s);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static void *plgfs_lnk_iop_follow_link(struct dentry *d, struct nameidata *nd)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	mm_segment_t old_fs;
	char *buf;
	void *rv;
	int len;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return ERR_CAST(cont);

	cont->op_id = PLGFS_LNK_IOP_FOLLOW_LINK;
	cont->op_args.i_follow_link.dentry = d;
	cont->op_args.i_follow_link.nd = nd;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.i_follow_link.dentry;
	nd = cont->op_args.i_follow_link.nd;

	buf = kzalloc(PATH_MAX, GFP_KERNEL);
	if (!buf) {
		cont->op_rv.rv_void = ERR_PTR(-ENOMEM);
		goto postcalls;
	}

	old_fs = get_fs();
	set_fs(get_ds());
	len = generic_readlink(plgfs_dh(d), buf, PATH_MAX);
	set_fs(old_fs);
	if (len < 0) {
		kfree(buf);
		cont->op_rv.rv_void = ERR_PTR(len);
		goto postcalls;
	}

	nd_set_link(nd, buf); 
postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_void;

	plgfs_free_context(sbi, cont);

	return rv;
}

static void plgfs_lnk_iop_put_link(struct dentry *d, struct nameidata *nd,
		void *cookie)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	char *buf;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return;

	cont->op_id = PLGFS_LNK_IOP_PUT_LINK;
	cont->op_args.i_put_link.dentry = d;
	cont->op_args.i_put_link.nd = nd;
	cont->op_args.i_put_link.cookie = cookie;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.i_put_link.dentry;
	nd = cont->op_args.i_put_link.nd;
	cookie = cont->op_args.i_put_link.cookie;

	buf = nd_get_link(nd);
	if (!IS_ERR(buf))
		kfree(buf);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	plgfs_free_context(sbi, cont);

	return;
}

static int plgfs_dir_iop_mknod(struct inode *ip, struct dentry *d, umode_t mode,
		dev_t dev)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *iph;
	struct dentry *dh;
	struct inode *i;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_MKNOD;
	cont->op_args.i_mknod.dir = ip;
	cont->op_args.i_mknod.dentry = d;
	cont->op_args.i_mknod.mode = mode;
	cont->op_args.i_mknod.dev = dev;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	ip = cont->op_args.i_mknod.dir;
	d = cont->op_args.i_mknod.dentry;
	mode = cont->op_args.i_mknod.mode;
	dev = cont->op_args.i_mknod.dev;

	iph = plgfs_ih(ip);
	dh = plgfs_dh(d);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_mknod(iph, dh, mode, dev);
	mutex_unlock(&iph->i_mutex);

	if (cont->op_rv.rv_int)
		goto postcalls;

	i = plgfs_iget(ip->i_sb, (unsigned long)dh->d_inode);
	if (IS_ERR(i)) {
		mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
		rv = vfs_unlink(iph, dh);
		mutex_unlock(&iph->i_mutex);
		if (rv)
			pr_err("pluginfs: mknod: unlink failed: %d\n", rv);

		cont->op_rv.rv_int = PTR_ERR(i);
		goto postcalls;
	}

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_inode_size(ip, iph);
	d_instantiate(d, i);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_dir_iop_link(struct dentry *dold, struct inode *ip,
		struct dentry *dnew)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *iph;
	struct inode *i;
	int rv;

	sbi = plgfs_sbi(ip->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_IOP_LINK;
	cont->op_args.i_link.old_dentry = dold;
	cont->op_args.i_link.dir = ip;
	cont->op_args.i_link.new_dentry = dnew;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	dold = cont->op_args.i_link.old_dentry;
	ip = cont->op_args.i_link.dir;
	dnew = cont->op_args.i_link.new_dentry;
	iph = plgfs_ih(ip);

	mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
	cont->op_rv.rv_int = vfs_link(plgfs_dh(dold), iph, plgfs_dh(dnew));
	mutex_unlock(&iph->i_mutex);

	if (cont->op_rv.rv_int)
		goto postcalls;

	i = plgfs_iget(ip->i_sb, (unsigned long)plgfs_dh(dnew)->d_inode);
	if (IS_ERR(i)) {
		mutex_lock_nested(&iph->i_mutex, I_MUTEX_PARENT);
		rv = vfs_unlink(iph, plgfs_dh(dnew));
		mutex_unlock(&iph->i_mutex);
		if (rv)
			pr_err("pluginfs: link: unlink failed: %d\n", rv);

		cont->op_rv.rv_int = PTR_ERR(i);
		goto postcalls;
	}

	fsstack_copy_attr_times(ip, iph);
	fsstack_copy_inode_size(ip, iph);
	set_nlink(dold->d_inode, plgfs_dh(dold)->d_inode->i_nlink);
	d_instantiate(dnew, i);
postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_iop_permission(struct inode *i, int mask, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	int rv;

	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_permission.inode = i;
	cont->op_args.i_permission.mask = mask;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	i = cont->op_args.i_permission.inode;
	mask = cont->op_args.i_permission.mask;

	cont->op_rv.rv_int = inode_permission(plgfs_ih(i), mask);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_lnk_iop_permission(struct inode *i, int mask)
{
	return plgfs_iop_permission(i, mask, PLGFS_LNK_IOP_PERMISSION);
}

static int plgfs_reg_iop_permission(struct inode *i, int mask)
{
	return plgfs_iop_permission(i, mask, PLGFS_REG_IOP_PERMISSION);
}

static int plgfs_dir_iop_permission(struct inode *i, int mask)
{
	return plgfs_iop_permission(i, mask, PLGFS_DIR_IOP_PERMISSION);
}

static int plgfs_iop_setxattr(struct dentry *d, const char *n,
		const void *v, size_t s, int f, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_setxattr.dentry = d;
	cont->op_args.i_setxattr.name = n;
	cont->op_args.i_setxattr.value = v;
	cont->op_args.i_setxattr.size = s;
	cont->op_args.i_setxattr.flags = f;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	
	d = cont->op_args.i_setxattr.dentry;
	n = cont->op_args.i_setxattr.name;
	v = cont->op_args.i_setxattr.value;
	s = cont->op_args.i_setxattr.size;
	f = cont->op_args.i_setxattr.flags;

	dh = plgfs_dh(d);

	cont->op_rv.rv_int = vfs_setxattr(dh, n, v, s, f);
	if (cont->op_rv.rv_int)
		goto postcalls;

	fsstack_copy_attr_all(d->d_inode, dh->d_inode);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_lnk_iop_setxattr(struct dentry *d, const char *n,
		const void *v, size_t s, int f)
{
	return plgfs_iop_setxattr(d, n, v, s, f, PLGFS_LNK_IOP_SETXATTR);
}

static int plgfs_reg_iop_setxattr(struct dentry *d, const char *n,
		const void *v, size_t s, int f)
{
	return plgfs_iop_setxattr(d, n, v, s, f, PLGFS_REG_IOP_SETXATTR);
}

static int plgfs_dir_iop_setxattr(struct dentry *d, const char *n,
		const void *v, size_t s, int f)
{
	return plgfs_iop_setxattr(d, n, v, s, f, PLGFS_DIR_IOP_SETXATTR);
}

static ssize_t plgfs_iop_getxattr(struct dentry *d, const char *n,
		void *v, size_t s, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	ssize_t rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_getxattr.dentry = d;
	cont->op_args.i_getxattr.name = n;
	cont->op_args.i_getxattr.value = v;
	cont->op_args.i_getxattr.size = s;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;
	
	d = cont->op_args.i_getxattr.dentry;
	n = cont->op_args.i_getxattr.name;
	v = cont->op_args.i_getxattr.value;
	s = cont->op_args.i_getxattr.size;

	cont->op_rv.rv_ssize = vfs_getxattr(plgfs_dh(d), n, v, s);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static ssize_t plgfs_lnk_iop_getxattr(struct dentry *d, const char *n,
		void *v, size_t s)
{
	return plgfs_iop_getxattr(d, n, v, s, PLGFS_LNK_IOP_GETXATTR);
}

static ssize_t plgfs_reg_iop_getxattr(struct dentry *d, const char *n,
		void *v, size_t s)
{
	return plgfs_iop_getxattr(d, n, v, s, PLGFS_REG_IOP_GETXATTR);
}

static ssize_t plgfs_dir_iop_getxattr(struct dentry *d, const char *n,
		void *v, size_t s)
{
	return plgfs_iop_getxattr(d, n, v, s, PLGFS_DIR_IOP_GETXATTR);
}

static ssize_t plgfs_iop_listxattr(struct dentry *d, char *l, size_t s,
		int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	ssize_t rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_listxattr.dentry = d;
	cont->op_args.i_listxattr.list = l;
	cont->op_args.i_listxattr.size = s;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;
	
	d = cont->op_args.i_listxattr.dentry;
	l = cont->op_args.i_listxattr.list;
	s = cont->op_args.i_listxattr.size;

	cont->op_rv.rv_ssize = vfs_listxattr(plgfs_dh(d), l, s);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static ssize_t plgfs_lnk_iop_listxattr(struct dentry *d, char *l, size_t s)
{
	return plgfs_iop_listxattr(d, l, s, PLGFS_LNK_IOP_LISTXATTR);
}

static ssize_t plgfs_reg_iop_listxattr(struct dentry *d, char *l, size_t s)
{
	return plgfs_iop_listxattr(d, l, s, PLGFS_REG_IOP_LISTXATTR);
}

static ssize_t plgfs_dir_iop_listxattr(struct dentry *d, char *l, size_t s)
{
	return plgfs_iop_listxattr(d, l, s, PLGFS_DIR_IOP_LISTXATTR);
}

static int plgfs_iop_removexattr(struct dentry *d, const char *n, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	int rv;

	sbi = plgfs_sbi(d->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.i_removexattr.dentry = d;
	cont->op_args.i_removexattr.name = n;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;
	
	d = cont->op_args.i_removexattr.dentry;
	n = cont->op_args.i_removexattr.name;

	cont->op_rv.rv_int = vfs_removexattr(plgfs_dh(d), n);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_lnk_iop_removexattr(struct dentry *d, const char *n)
{
	return plgfs_iop_removexattr(d, n, PLGFS_LNK_IOP_REMOVEXATTR);
}

static int plgfs_reg_iop_removexattr(struct dentry *d, const char *n)
{
	return plgfs_iop_removexattr(d, n, PLGFS_REG_IOP_REMOVEXATTR);
}

static int plgfs_dir_iop_removexattr(struct dentry *d, const char *n)
{
	return plgfs_iop_removexattr(d, n, PLGFS_DIR_IOP_REMOVEXATTR);
}

static const struct inode_operations plgfs_lnk_iops= {
	.setattr = plgfs_lnk_iop_setattr,
	.getattr = plgfs_lnk_iop_getattr,
	.readlink = plgfs_lnk_iop_readlink,
	.follow_link = plgfs_lnk_iop_follow_link,
	.put_link = plgfs_lnk_iop_put_link,
	.permission = plgfs_lnk_iop_permission,
	.setxattr = plgfs_lnk_iop_setxattr,
	.getxattr = plgfs_lnk_iop_getxattr,
	.listxattr = plgfs_lnk_iop_listxattr,
	.removexattr = plgfs_lnk_iop_removexattr
};

static const struct inode_operations plgfs_reg_iops= {
	.setattr = plgfs_reg_iop_setattr,
	.getattr = plgfs_reg_iop_getattr,
	.permission = plgfs_reg_iop_permission,
	.setxattr = plgfs_reg_iop_setxattr,
	.getxattr = plgfs_reg_iop_getxattr,
	.listxattr = plgfs_reg_iop_listxattr,
	.removexattr = plgfs_reg_iop_removexattr
};

static const struct inode_operations plgfs_dir_iops= {
	.lookup = plgfs_dir_iop_lookup,
	.create = plgfs_dir_iop_create,
	.unlink = plgfs_dir_iop_unlink,
	.mkdir = plgfs_dir_iop_mkdir,
	.rmdir = plgfs_dir_iop_rmdir,
	.setattr = plgfs_dir_iop_setattr,
	.getattr = plgfs_dir_iop_getattr,
	.rename = plgfs_dir_iop_rename,
	.symlink = plgfs_dir_iop_symlink,
	.mknod = plgfs_dir_iop_mknod,
	.link = plgfs_dir_iop_link,
	.permission = plgfs_dir_iop_permission,
	.setxattr = plgfs_dir_iop_setxattr,
	.getxattr = plgfs_dir_iop_getxattr,
	.listxattr = plgfs_dir_iop_listxattr,
	.removexattr = plgfs_dir_iop_removexattr
};

static int plgfs_inode_test(struct inode *i, void *ih)
{
	if (plgfs_ih(i) == (struct inode *)ih)
		return 1;
	return 0;
}

static int plgfs_inode_set(struct inode *i, void *data)
{
	struct plgfs_inode_info *ii;
	struct inode *ih;

	ih = (struct inode *)data;

	ii = plgfs_alloc_ii(plgfs_sbi(i->i_sb));
	if (IS_ERR(ii))
		return PTR_ERR(ii);

	i->i_private = ii;
	i->i_ino = ih->i_ino;
	ii->inode_hidden = ih;
	fsstack_copy_attr_all(i, ih);
	fsstack_copy_inode_size(i, ih);

	if (S_ISREG(i->i_mode)) {
		i->i_op = &plgfs_reg_iops;
		i->i_fop = &plgfs_reg_fops;
	} else if (S_ISDIR(i->i_mode)) {
		i->i_op = &plgfs_dir_iops;
		i->i_fop = &plgfs_dir_fops;
	} else if (S_ISLNK(i->i_mode)) {
		i->i_op = &plgfs_lnk_iops;
	} else if (special_file(i->i_mode)) {
		init_special_inode(i, i->i_mode, i->i_rdev);
	}

	return 0;
}

struct inode *plgfs_iget(struct super_block *sb, unsigned long ino)
{
	struct inode *i;
	struct inode *ih;

	ih = (struct inode *)ino;

	if (!igrab(ih))
		return ERR_PTR(-ESTALE);

	i = iget5_locked(sb, ino, plgfs_inode_test, plgfs_inode_set, ih);
	if (!i) {
		iput(ih);
		return ERR_PTR(-ENOMEM);
	}

	if (!(i->i_state & I_NEW)) {
		fsstack_copy_attr_all(i, ih);
		fsstack_copy_inode_size(i, ih);
		iput(ih);
		return i;
	}

	unlock_new_inode(i);

	return i;
}

struct plgfs_inode_info *plgfs_alloc_ii(struct plgfs_sb_info *sbi)
{	
	struct plgfs_inode_info *ii;

	ii = kmem_cache_zalloc(sbi->cache->ii_cache, GFP_KERNEL);
	if (!ii)
		return ERR_PTR(-ENOMEM);

	mutex_init(&ii->file_hidden_mutex);

	return ii;
}
