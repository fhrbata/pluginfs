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

#ifndef __PLGFS_H__
#define __PLGFS_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/mount.h>
#include <linux/fs_stack.h>
#include <linux/stat.h>
#include <linux/namei.h>
#include <linux/cred.h>
#include <linux/path.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/dcache.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/blk_types.h>
#include <linux/aio.h>
#include <linux/string.h>
#include "pluginfs.h"

#define PLGFS_VERSION "0.001"

#define PLGFS_MAGIC 0x504C47

#define PLGFS_OPT_DIFF_PLGS 1

struct plgfs_mnt_cfg {
	int  plgs_nr;
	struct block_device *bdev;
	fmode_t mode;
	struct plgfs_plugin **plgs;
	char *plgs_str;
	char *fstype_str;
	struct path path;
	unsigned int flags;
	char opts[PAGE_SIZE];
};

extern struct plgfs_mnt_cfg *plgfs_get_cfg(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data);

extern void plgfs_put_cfg(struct plgfs_mnt_cfg *cfg);

struct plgfs_cache {
	struct kmem_cache *fi_cache; /* file info cache */
	struct kmem_cache *di_cache; /* dentry info cache */
	struct kmem_cache *ii_cache; /* inode info cache */
	struct kmem_cache *ci_cache; /* context info cache */
	struct list_head list;
	int count;
	int plg_nr;
};

extern struct plgfs_cache *plgfs_cache_get(int);
extern void plgfs_cache_put(struct plgfs_cache *);

struct plgfs_dev {
	struct block_device *bdev;
	struct block_device *bdev_hidden;
	struct request_queue *queue;
	struct gendisk *gd;
	fmode_t mode;
	int minor;
	int count;
};

extern struct plgfs_dev *plgfs_add_dev(struct block_device *bdev, fmode_t mode);
extern void plgfs_rem_dev(struct plgfs_dev *pdev);

struct plgfs_sb_info {
	struct vfsmount *mnt_hidden;
	struct plgfs_dev *pdev;
	struct path path_hidden;
	struct plgfs_cache *cache;
	struct mutex mutex_walk;
	struct plgfs_plugin **plgs;
	unsigned int plgs_nr;
	void **priv;
	void *data[0];
};

static inline struct plgfs_sb_info *plgfs_sbi(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern int plgfs_fill_super(struct super_block *, int, struct plgfs_mnt_cfg *);

struct plgfs_dentry_info {
	struct dentry *dentry_hidden;
	struct dentry *dentry_walk;
	struct list_head list_walk; /* pretected by mutex_walk in sbi */
	void *priv[0];
};

static inline struct plgfs_dentry_info *plgfs_di(struct dentry *d)
{
	return d->d_fsdata;
}

static inline struct dentry *plgfs_dh(struct dentry *d)
{
	return plgfs_di(d)->dentry_hidden;
}

extern struct plgfs_dentry_info *plgfs_alloc_di(struct dentry *, struct dentry *);

extern const struct dentry_operations plgfs_dops;

struct plgfs_inode_info {
	struct inode *inode_hidden;
	struct file *file_hidden;
	int file_hidden_cnt;
	struct mutex file_hidden_mutex;
	void *priv[0];
};

static inline struct plgfs_inode_info *plgfs_ii(struct inode *i)
{
	return i->i_private;
}

static inline struct inode *plgfs_ih(struct inode *i)
{
	return plgfs_ii(i)->inode_hidden;
}

extern struct plgfs_inode_info *plgfs_alloc_ii(struct plgfs_sb_info *sbi);
extern struct inode *plgfs_iget(struct super_block *, unsigned long);

struct plgfs_file_info {
	void *priv[0];
};

extern struct plgfs_file_info *plgfs_alloc_fi(struct file *);

static inline struct plgfs_file_info *plgfs_fi(struct file *f)
{
	return f->private_data;
}

static inline struct file *plgfs_fh(struct file *f)
{
	return plgfs_ii(f->f_dentry->d_inode)->file_hidden;
}

extern const struct file_operations plgfs_reg_fops;
extern const struct file_operations plgfs_dir_fops;

extern struct plgfs_plugin *plgfs_get_plg(const char *);
extern inline void plgfs_put_plg(struct plgfs_plugin *);
extern void plgfs_put_plgs(struct plgfs_plugin **, int);

extern int plgfs_precall_plgs(struct plgfs_context *, struct plgfs_sb_info *);
extern void plgfs_postcall_plgs(struct plgfs_context *, struct plgfs_sb_info *);

extern struct plgfs_context *plgfs_alloc_context(struct plgfs_sb_info *);
extern void plgfs_free_context(struct plgfs_sb_info *, struct plgfs_context *);

extern int plgfs_show_options(struct seq_file *, struct dentry *);

extern struct file_system_type plgfs_type;

extern int plgfs_major;

#endif

