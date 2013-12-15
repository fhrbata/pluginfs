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

#ifndef __PLUGINFS_H__
#define __PLUGINFS_H__

#include <linux/module.h>
#include <linux/fs.h>

enum plgfs_op_id {
	PLGFS_DOP_D_RELEASE,
	PLGFS_DOP_D_REVALIDATE,
	PLGFS_REG_FOP_OPEN,
	PLGFS_REG_FOP_RELEASE,
	PLGFS_REG_FOP_LLSEEK,
	PLGFS_REG_FOP_READ,
	PLGFS_REG_FOP_WRITE,
	PLGFS_REG_FOP_FSYNC,
	PLGFS_REG_FOP_MMAP,
	PLGFS_REG_IOP_SETATTR,
	PLGFS_REG_IOP_GETATTR,
	PLGFS_REG_IOP_PERMISSION,
	PLGFS_DIR_IOP_UNLINK,
	PLGFS_DIR_IOP_MKDIR,
	PLGFS_DIR_IOP_RMDIR,
	PLGFS_DIR_IOP_SYMLINK,
	PLGFS_DIR_IOP_SETATTR,
	PLGFS_DIR_IOP_GETATTR,
	PLGFS_DIR_IOP_PERMISSION,
	PLGFS_DIR_FOP_OPEN,
	PLGFS_DIR_FOP_RELEASE,
	PLGFS_DIR_FOP_ITERATE,
	PLGFS_DIR_FOP_LLSEEK,
	PLGFS_DIR_IOP_LOOKUP,
	PLGFS_DIR_IOP_CREATE,
	PLGFS_DIR_IOP_RENAME,
	PLGFS_DIR_IOP_MKNOD,
	PLGFS_DIR_IOP_LINK,
	PLGFS_LNK_IOP_SETATTR,
	PLGFS_LNK_IOP_GETATTR,
	PLGFS_LNK_IOP_READLINK,
	PLGFS_LNK_IOP_FOLLOW_LINK,
	PLGFS_LNK_IOP_PUT_LINK,
	PLGFS_LNK_IOP_PERMISSION,
	PLGFS_OP_NR
};

enum plgfs_op_call {
	PLGFS_PRECALL,
	PLGFS_POSTCALL
};

union plgfs_op_rv {
	int		rv_int;
	ssize_t		rv_ssize;
	unsigned int	rv_uint;
	unsigned long	rv_ulong;
	loff_t		rv_loff;
	struct dentry	*rv_dentry;
	sector_t	rv_sector;
	struct page	*rv_page;
	void		*rv_void;
};

union plgfs_op_args {
	struct {
		struct inode *inode;
		struct file *file;
	} f_open;

	struct {
		struct inode *inode;
		struct file *file;
	} f_release;

	struct {
		struct file *file;
		struct dir_context *ctx;
	} f_iterate;

	struct {
		struct file *file;
		loff_t offset;
		int origin;
	} f_llseek;

	struct {
		struct file *file;
		char __user *buf;
		ssize_t count;
		loff_t *pos;
	} f_read;

	struct {
		struct file *file;
		const char __user *buf;
		ssize_t count;
		loff_t *pos;
	} f_write;

	struct {
		struct file *file;
		loff_t start;
		loff_t end;
		int datasync;
	} f_fsync;

	struct {
		struct file *file;
		struct vm_area_struct *vma;
	} f_mmap;

	struct {
		struct dentry *dentry;
	} d_release;

	struct {
		struct dentry *dentry;
		unsigned int flags;
	} d_revalidate;

	struct {
		struct dentry *dentry;
		struct iattr *iattr;
	} i_setattr;

	struct {
		struct vfsmount *mnt;
		struct dentry *dentry;
		struct kstat *stat;
	} i_getattr;

	struct {
		struct inode *inode;
		int mask;
	} i_permission;

	struct {
		struct dentry *dentry;
		char __user *buffer;
		int buflen;
	} i_readlink;

	struct {
		struct dentry *dentry;
		struct nameidata *nd;
	} i_follow_link;

	struct {
		struct dentry *dentry;
		struct nameidata *nd;
		void *cookie;
	} i_put_link;

	struct {
		struct inode *dir;
		struct dentry *dentry;
	} i_unlink;

	struct {
		struct inode *dir;
		struct dentry *dentry;
		umode_t mode;
	} i_mkdir;

	struct {
		struct inode *dir;
		struct dentry *dentry;
	} i_rmdir;

	struct {
		struct inode *dir;
		struct dentry *dentry;
		const char *name;
	} i_symlink;

	struct {
		struct inode *dir;
		struct dentry *dentry;
		unsigned int flags;
	} i_lookup;

	struct {
		struct inode *dir;
		struct dentry *dentry;
		umode_t mode;
		bool excl;
	} i_create;

	struct {
		struct inode *old_dir;
		struct dentry *old_dentry;
		struct inode *new_dir;
		struct dentry *new_dentry;
	} i_rename;

	struct {
		struct inode *dir;
		struct dentry *dentry;
		umode_t mode;
		dev_t dev;
	} i_mknod;

	struct {
		struct dentry *old_dentry;
		struct inode *dir;
		struct dentry *new_dentry;
	} i_link;
};

enum plgfs_rv {
	PLGFS_CONTINUE,
	PLGFS_STOP
};

struct plgfs_context {
	enum plgfs_op_id op_id;
	enum plgfs_op_call op_call;
	union plgfs_op_args op_args;
	union plgfs_op_rv op_rv;
	struct plgfs_plugin *plg;
	int plg_id;
	int idx_start;
	int idx_end;
	void *priv[1];
};

typedef enum plgfs_rv (*plgfs_op_cb)(struct plgfs_context *);

struct plgfs_op_cbs {
	plgfs_op_cb pre;
	plgfs_op_cb post;
};

struct plgfs_plugin {
	struct module *owner;
	char *name;
	int priority;
	struct plgfs_op_cbs *cbs;
	struct list_head list;
};

extern int plgfs_register_plugin(struct plgfs_plugin *);
extern int plgfs_unregister_plugin(struct plgfs_plugin *);
extern int plgfs_get_plugin_sb_id(struct plgfs_plugin *, struct super_block *);

extern inline void *plgfs_get_sb_priv(struct super_block *, int);
extern inline void plgfs_set_sb_priv(struct super_block *, int, void *);
extern inline void *plgfs_get_file_priv(struct file *, int);
extern inline void plgfs_set_file_priv(struct file *, int, void *);
extern inline void *plgfs_get_dentry_priv(struct dentry *, int);
extern inline void plgfs_set_dentry_priv(struct dentry *, int, void *);
extern inline void *plgfs_get_inode_priv(struct inode *, int);
extern inline void plgfs_set_inode_priv(struct inode *, int, void *);

extern int plgfs_walk_dtree(struct plgfs_plugin *, struct dentry *,
		int (*cb)(struct dentry *, void *, int), void *);

extern void plgfs_pass_on_option(char *, char *);

#endif
