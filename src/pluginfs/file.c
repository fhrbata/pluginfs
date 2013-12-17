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

static struct file *plgfs_get_fh(struct file *f)
{
	struct path path;
	int flags;

	path.mnt = plgfs_sbi(f->f_path.mnt->mnt_sb)->path_hidden.mnt;
	path.dentry = plgfs_dh(f->f_dentry);

	flags = f->f_flags;

	if (f->f_mode == (FMODE_READ | FMODE_WRITE))
		flags |= O_RDWR;
	else if (f->f_mode == FMODE_READ)
		flags |= O_RDONLY;
	else if (f->f_mode == FMODE_WRITE)
		flags |= O_WRONLY;

	return dentry_open(&path, flags, current_cred());
}

static void plgfs_put_fh(struct file *f)
{
	struct file *fh;

	fh = plgfs_fh(f);

	if (fh)
		fput(fh);
}

static int plgfs_fop_open(struct inode *i, struct file *f, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct plgfs_file_info *fi;
	int rv;

	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.f_open.inode = i;
	cont->op_args.f_open.file = f;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	fi = plgfs_alloc_fi(f);
	if (IS_ERR(fi)) {
		cont->op_rv.rv_int = PTR_ERR(fi);
		goto postcalls;
	}

	f->private_data = fi;

	fi->file_hidden = plgfs_get_fh(f);
	if (IS_ERR(fi->file_hidden))
		cont->op_rv.rv_int = PTR_ERR(fi->file_hidden);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_fop_open(struct inode *i, struct file *f)
{
	return plgfs_fop_open(i, f, PLGFS_REG_FOP_OPEN);
}

static int plgfs_dir_fop_open(struct inode *i, struct file *f)
{
	return plgfs_fop_open(i, f, PLGFS_DIR_FOP_OPEN);
}

static int plgfs_fop_release(struct inode *i, struct file *f, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	int rv;

	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont)) {
		fput(plgfs_fh(f));
		kmem_cache_free(sbi->cache->fi_cache, plgfs_fi(f));
		pr_err("pluginfs: cannot alloc context for file release, no"
				"plugins will be called\n");
		return PTR_ERR(cont);
	}

	cont->op_id = op_id;
	cont->op_args.f_release.inode = i;
	cont->op_args.f_release.file = f;

	plgfs_precall_plgs(cont, sbi);

	plgfs_put_fh(f);

	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	kmem_cache_free(sbi->cache->fi_cache, plgfs_fi(f));

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_fop_release(struct inode *i, struct file *f)
{
	return plgfs_fop_release(i, f, PLGFS_REG_FOP_RELEASE);
}

static int plgfs_dir_fop_release(struct inode *i, struct file *f)
{
	return plgfs_fop_release(i, f, PLGFS_DIR_FOP_RELEASE);
}

static loff_t plgfs_fop_llseek(struct file *f, loff_t offset, int origin,
		int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *i;
	loff_t rv;

	i = f->f_dentry->d_inode;
	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.f_llseek.file = f;
	cont->op_args.f_llseek.offset = offset;
	cont->op_args.f_llseek.origin = origin;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_llseek.file;
	offset = cont->op_args.f_llseek.offset;
	origin = cont->op_args.f_llseek.origin;

	cont->op_rv.rv_loff = generic_file_llseek(f, offset, origin);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static loff_t plgfs_reg_fop_llseek(struct file *f, loff_t offset, int origin)
{
	return plgfs_fop_llseek(f, offset, origin, PLGFS_REG_FOP_LLSEEK);
}

static loff_t plgfs_dir_fop_llseek(struct file *f, loff_t offset, int origin)
{
	return plgfs_fop_llseek(f, offset, origin, PLGFS_DIR_FOP_LLSEEK);
}

static int plgfs_dir_fop_iterate(struct file *f, struct dir_context *ctx)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct inode *i;
	int rv;

	i = f->f_dentry->d_inode;
	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DIR_FOP_ITERATE;
	cont->op_args.f_iterate.file = f;
	cont->op_args.f_iterate.ctx = ctx;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_iterate.file;
	ctx = cont->op_args.f_iterate.ctx;

	cont->op_rv.rv_int = iterate_dir(plgfs_fh(f), ctx);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static ssize_t plgfs_reg_fop_read(struct file *f, char __user *buf, size_t count,
		loff_t *pos)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	struct inode *i;
	ssize_t rv;

	i = f->f_dentry->d_inode;
	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_REG_FOP_READ;
	cont->op_args.f_read.file = f;
	cont->op_args.f_read.buf = buf;
	cont->op_args.f_read.count = count;
	cont->op_args.f_read.pos = pos;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_read.file;
	buf = cont->op_args.f_read.buf;
	count = cont->op_args.f_read.count;
	pos = cont->op_args.f_read.pos;
	i = f->f_dentry->d_inode;

	fh = plgfs_fh(f);

	cont->op_rv.rv_ssize = kernel_read(fh,
			*cont->op_args.f_read.pos,
			cont->op_args.f_read.buf,
			cont->op_args.f_read.count);

	if (cont->op_rv.rv_ssize < 0)
		goto postcalls;

	*pos += cont->op_rv.rv_ssize;

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_ssize;

	plgfs_free_context(sbi, cont);

	return rv;
}

static ssize_t plgfs_reg_fop_write(struct file *f, const char __user *buf, size_t count,
		loff_t *pos)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	struct inode *i;
	ssize_t rv;

	i = f->f_dentry->d_inode;
	sbi = plgfs_sbi(i->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_REG_FOP_WRITE;
	cont->op_args.f_write.file = f;
	cont->op_args.f_write.buf = buf;
	cont->op_args.f_write.count = count;
	cont->op_args.f_write.pos = pos;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_write.file;
	buf = cont->op_args.f_write.buf;
	count = cont->op_args.f_write.count;
	pos = cont->op_args.f_write.pos;
	i = f->f_dentry->d_inode;

	fh = plgfs_fh(f);

	cont->op_rv.rv_ssize = kernel_write(fh, buf, count, *pos);

	if (cont->op_rv.rv_ssize < 0)
		goto postcalls;

	*pos += cont->op_rv.rv_ssize;

	if (*pos <= i_size_read(i))
		goto postcalls;

	i_size_write(i, *pos);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_ssize;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_fop_fsync(struct file *f, loff_t s, loff_t e, int d)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	ssize_t rv;

	sbi = plgfs_sbi(f->f_dentry->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_REG_FOP_FSYNC;
	cont->op_args.f_fsync.file = f;
	cont->op_args.f_fsync.start = s;
	cont->op_args.f_fsync.end = e;
	cont->op_args.f_fsync.datasync = d;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_fsync.file;
	d = cont->op_args.f_fsync.datasync;

	cont->op_rv.rv_int = vfs_fsync(plgfs_fh(f), d);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_ssize;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_fop_mmap(struct file *f, struct vm_area_struct *v)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	ssize_t rv;

	sbi = plgfs_sbi(f->f_dentry->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_REG_FOP_MMAP;
	cont->op_args.f_mmap.file = f;
	cont->op_args.f_mmap.vma = v;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_mmap.file;
	v = cont->op_args.f_mmap.vma;
	fh = plgfs_fh(f);

	cont->op_rv.rv_int = -ENODEV;
	if (!fh->f_op->mmap)
		goto postcalls;

	v->vm_file = get_file(fh);
	cont->op_rv.rv_int = fh->f_op->mmap(fh, v);
	if (cont->op_rv.rv_int) {
		fput(fh);
		v->vm_file = f;
	} else
		fput(f);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_ssize;

	plgfs_free_context(sbi, cont);

	return rv;
}

#ifdef CONFIG_COMPAT
static long plgfs_fop_compat_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	long rv;

	sbi = plgfs_sbi(f->f_dentry->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.f_compat_ioctl.file = f;
	cont->op_args.f_compat_ioctl.cmd = cmd;
	cont->op_args.f_compat_ioctl.arg = arg;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_compat_ioctl.file;
	cmd = cont->op_args.f_compat_ioctl.cmd;
	arg = cont->op_args.f_compat_ioctl.arg;

	fh = plgfs_fh(f);

	cont->op_rv.rv_long = -ENOIOCTLCMD;

	if (!fh->f_op || !fh->f_op->compat_ioctl)
		goto postcalls;

	cont->op_rv.rv_long = fh->f_op->compat_ioctl(fh, cmd, arg);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_long;

	plgfs_free_context(sbi, cont);

	return rv;
}
#endif

static long plgfs_reg_fop_compat_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg)
{
	return plgfs_fop_compat_ioctl(f, cmd, arg, PLGFS_REG_FOP_COMPAT_IOCTL);
}

static long plgfs_dir_fop_compat_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg)
{
	return plgfs_fop_compat_ioctl(f, cmd, arg, PLGFS_DIR_FOP_COMPAT_IOCTL);
}

static long plgfs_fop_unlocked_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	long rv;

	sbi = plgfs_sbi(f->f_dentry->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.f_unlocked_ioctl.file = f;
	cont->op_args.f_unlocked_ioctl.cmd = cmd;
	cont->op_args.f_unlocked_ioctl.arg = arg;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_unlocked_ioctl.file;
	cmd = cont->op_args.f_unlocked_ioctl.cmd;
	arg = cont->op_args.f_unlocked_ioctl.arg;

	fh = plgfs_fh(f);

	cont->op_rv.rv_long = -ENOTTY;

	if (!fh->f_op || !fh->f_op->unlocked_ioctl)
		goto postcalls;

	cont->op_rv.rv_long = fh->f_op->unlocked_ioctl(fh, cmd, arg);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_long;

	plgfs_free_context(sbi, cont);

	return rv;
}

static long plgfs_reg_fop_unlocked_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg)
{
	return plgfs_fop_unlocked_ioctl(f, cmd, arg,
			PLGFS_REG_FOP_UNLOCKED_IOCTL);
}

static long plgfs_dir_fop_unlocked_ioctl(struct file *f, unsigned int cmd,
		unsigned long arg)
{
	return plgfs_fop_unlocked_ioctl(f, cmd, arg,
			PLGFS_DIR_FOP_UNLOCKED_IOCTL);
}

static int plgfs_fop_flush(struct file *f, fl_owner_t id, int op_id)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct file *fh;
	int rv;

	sbi = plgfs_sbi(f->f_dentry->d_inode->i_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = op_id;
	cont->op_args.f_flush.file = f;
	cont->op_args.f_flush.id = id;

	if (!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	f = cont->op_args.f_flush.file;
	id = cont->op_args.f_flush.id;

	fh = plgfs_fh(f);

	cont->op_rv.rv_int = 0;

	if (!fh->f_op || !fh->f_op->flush)
		goto postcalls;

	cont->op_rv.rv_int = fh->f_op->flush(fh, id);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_reg_fop_flush(struct file *f, fl_owner_t id)
{
	return plgfs_fop_flush(f, id, PLGFS_REG_FOP_FLUSH);
}

static int plgfs_dir_fop_flush(struct file *f, fl_owner_t id)
{
	return plgfs_fop_flush(f, id, PLGFS_DIR_FOP_FLUSH);
}

const struct file_operations plgfs_reg_fops = {
	.open = plgfs_reg_fop_open,
	.release = plgfs_reg_fop_release,
	.read = plgfs_reg_fop_read,
	.write = plgfs_reg_fop_write,
	.llseek = plgfs_reg_fop_llseek,
	.fsync = plgfs_reg_fop_fsync,
	.mmap = plgfs_reg_fop_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl = plgfs_reg_fop_compat_ioctl,
#endif
	.unlocked_ioctl = plgfs_reg_fop_unlocked_ioctl,
	.flush = plgfs_reg_fop_flush
};

const struct file_operations plgfs_dir_fops = {
	.open = plgfs_dir_fop_open,
	.release = plgfs_dir_fop_release,
	.iterate = plgfs_dir_fop_iterate,
	.llseek = plgfs_dir_fop_llseek,
#ifdef CONFIG_COMPAT
	.compat_ioctl = plgfs_dir_fop_compat_ioctl,
#endif
	.unlocked_ioctl = plgfs_dir_fop_unlocked_ioctl,
	.flush = plgfs_dir_fop_flush
};

struct plgfs_file_info *plgfs_alloc_fi(struct file *f)
{
	struct plgfs_sb_info *sbi;
	struct plgfs_file_info *fi;

	sbi = plgfs_sbi(f->f_dentry->d_sb);
	fi = kmem_cache_zalloc(sbi->cache->fi_cache, GFP_KERNEL);
	if (!fi)
		return ERR_PTR(-ENOMEM);

	return fi;
}
