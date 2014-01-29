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

static struct kmem_cache *avplg_inode_info_cache = NULL;

static int avplg_should_check(struct file *file)
{
	if (avplg_task_allow(current->tgid))
		return 0;

	if (avplg_trusted_allow(current->tgid))
		return 0;

	if (!i_size_read(file->f_dentry->d_inode))
		return 0;

	return 1;
}

static enum plgfs_rv avplg_eval_res(int rv, struct plgfs_context *cont)
{
	if (rv == AVPLG_ACCESS_DENY) 
		rv = -EPERM;

	if (rv < 0) {
		cont->op_rv.rv_int = rv;
		return PLGFS_STOP;
	}

	cont->op_rv.rv_int = 0;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_open(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct avplg_inode_info *ii;
	struct file *file;
	int rv = 0;
	int wc;

	file = cont->op_args.f_open.file;

	if (!avplg_should_check(file))
		return PLGFS_CONTINUE;

	ii = avplg_ii(file->f_dentry->d_inode, cont->plg_id);
	if (!(atomic_read(&ii->path_info) & AVPLG_I_INCL)) 
		return PLGFS_CONTINUE;

	wc = atomic_read(&file->f_dentry->d_inode->i_writecount);
	sbi = avplg_sbi(file->f_dentry->d_sb, cont->plg_id);

	rv = avplg_icache_check(file, cont->plg_id);
	if (!avplg_sb_nocache(sbi) && rv) {
		if (wc <= 0)
			return avplg_eval_res(rv, cont);

		else if (wc == 1 && file->f_mode & FMODE_WRITE)
			return avplg_eval_res(rv, cont);

		else if (avplg_sb_nowrite(sbi))
			return avplg_eval_res(rv, cont);
	}

	rv = avplg_event_process(file, AVPLG_EVENT_OPEN, cont->plg_id);

	return avplg_eval_res(rv, cont);
}

static enum plgfs_rv avplg_post_release(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct avplg_inode_info *ii;
	struct file *file;
	int rv = 0;

	file = cont->op_args.f_release.file;

	if (!avplg_should_check(file))
		return PLGFS_CONTINUE;

	if (file->f_mode & FMODE_WRITE)
		avplg_icache_inv(file, cont->plg_id);

	ii = avplg_ii(file->f_dentry->d_inode, cont->plg_id);
	if (!(atomic_read(&ii->path_info) & AVPLG_I_INCL)) 
		return PLGFS_CONTINUE;

	sbi = avplg_sbi(file->f_dentry->d_sb, cont->plg_id);

	rv = avplg_icache_check(file, cont->plg_id);
	if (!avplg_sb_nocache(sbi) && rv)
		return avplg_eval_res(rv, cont);

	if (avplg_sb_noclose(sbi) || !(file->f_mode & FMODE_WRITE))
		return avplg_eval_res(rv, cont);

	rv = avplg_event_process(file, AVPLG_EVENT_CLOSE, cont->plg_id);

	return avplg_eval_res(rv, cont);
}

enum avplg_options {
	opt_timeout,
	opt_close,
	opt_cache,
	opt_write,
	opt_noclose,
	opt_nocache,
	opt_nowrite,
	opt_incl,
	opt_excl,
	opt_unknown
};

static match_table_t avplg_tokens = {
	{opt_timeout, "avplg_timeout=%u"},
	{opt_close, "avplg_close"},
	{opt_cache, "avplg_cache"},
	{opt_cache, "avplg_write"},
	{opt_noclose, "avplg_noclose"},
	{opt_nocache, "avplg_nocache"},
	{opt_nowrite, "avplg_nowrite"},
	{opt_incl, "avplg_incl=%s"},
	{opt_excl, "avplg_excl=%s"},
	{opt_unknown, NULL}
};

struct avplg_sb_info *avplg_sbi(struct super_block *sb, int id)
{
	return *plgfs_sb_priv(sb, id);
}

static void avplg_set_flags(struct avplg_sb_info *sbi, unsigned int flags)
{
	sbi->flags = flags;
	wmb();
}

static unsigned int avplg_get_flags(struct avplg_sb_info *sbi)
{
	rmb();
	return sbi->flags;
}

unsigned int avplg_sb_noclose(struct avplg_sb_info *sbi)
{
	return avplg_get_flags(sbi) & AVPLG_NOCLOSE;
}

unsigned int avplg_sb_nocache(struct avplg_sb_info *sbi)
{
	return avplg_get_flags(sbi) & AVPLG_NOCACHE;
}

unsigned int avplg_sb_nowrite(struct avplg_sb_info *sbi)
{
	return avplg_get_flags(sbi) & AVPLG_NOWRITE;
}

static void avplg_set_timeout(struct avplg_sb_info *sbi, unsigned long timeout)
{
	sbi->jiffies = timeout;
	wmb();
}

unsigned long avplg_sb_timeout(struct avplg_sb_info *sbi)
{
	rmb();
	return sbi->jiffies;
}

static int avplg_set_opts(struct avplg_sb_info *sbi, char *opts_in,
		char *opts_out)
{
	substring_t args[MAX_OPT_ARGS];
	char *opt;
	int token;
	unsigned int flags;
	unsigned int timeout;
	unsigned long jiffies;

	flags = AVPLG_NOCLOSE | AVPLG_NOWRITE;
	jiffies = MAX_SCHEDULE_TIMEOUT;
	if (!opts_in)
		return 0;

	while ((opt = strsep(&opts_in, ",")) != NULL) {

		if (!*opt)
			continue;

		token = match_token(opt, avplg_tokens, args);

		switch (token) {
			case opt_timeout:
				if (sscanf(args[0].from, "%u", &timeout) != 1)
					return -EINVAL;

				jiffies = msecs_to_jiffies(timeout);
				break;

			case opt_close:
				flags &= ~AVPLG_NOCLOSE;
				break;

			case opt_cache:
				flags &= ~AVPLG_NOCACHE;
				break;

			case opt_write:
				flags &= ~AVPLG_NOWRITE;
				break;

			case opt_noclose:
				flags |= AVPLG_NOCLOSE;
				break;

			case opt_nocache:
				flags |= AVPLG_NOCACHE;
				break;

			case opt_nowrite:
				flags |= AVPLG_NOWRITE;
				break;

			case opt_incl:
				sbi->incl_str = kstrdup(args[0].from,
						GFP_KERNEL);
				if (!sbi->incl_str)
					return -ENOMEM;
				break;

			case opt_excl:
				sbi->excl_str = kstrdup(args[0].from,
						GFP_KERNEL);
				if (!sbi->excl_str)
					return -ENOMEM;
				break;

			case opt_unknown:
				plgfs_pass_on_option(opt, opts_out);
				break;
		}
	}

	avplg_set_flags(sbi, flags);
	avplg_set_timeout(sbi, jiffies);

	return 0;
}

static struct avplg_sb_info *avplg_sbi_alloc(void)
{
	struct avplg_sb_info *sbi;

	sbi = kzalloc(sizeof(struct avplg_sb_info), GFP_KERNEL);
	if (!sbi)
		return ERR_PTR(-ENOMEM);

	mutex_init(&sbi->mutex);
	INIT_LIST_HEAD(&sbi->paths);

	return sbi;
}

static void avplg_sbi_free(struct avplg_sb_info *sbi)
{
	kfree(sbi->incl_str);
	kfree(sbi->excl_str);
	kfree(sbi);
}

static enum plgfs_rv avplg_pre_mount(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	char *opts_in;
	char *opts_out;

	sbi = avplg_sbi_alloc();
	if (!sbi) {
		cont->op_rv.rv_int = -ENOMEM;
		return PLGFS_STOP;
	}

	opts_in = cont->op_args.t_mount.opts_in;
	opts_out = cont->op_args.t_mount.opts_out;

	cont->op_rv.rv_int = avplg_set_opts(sbi, opts_in, opts_out);
	if (cont->op_rv.rv_int) {
		avplg_sbi_free(sbi);
		return PLGFS_STOP;
	}

	*plgfs_sb_priv(cont->op_args.t_mount.sb, cont->plg_id) = sbi;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_post_mount(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct super_block *sb;

	sb = cont->op_args.t_mount.sb;
	sbi = avplg_sbi(sb, cont->plg_id);
	if (cont->op_rv.rv_int)
		avplg_sbi_free(sbi);

	mutex_lock(&sbi->mutex);
	/* by default include root path "/" */
	if (!sbi->incl_str)
		avplg_set_paths(sb, "/", cont->plg_id, 1);
	avplg_set_paths(sb, sbi->incl_str, cont->plg_id, 1);
	avplg_set_paths(sb, sbi->excl_str, cont->plg_id, 0);
	mutex_unlock(&sbi->mutex);

	kfree(sbi->incl_str);
	kfree(sbi->excl_str);
	sbi->incl_str = sbi->excl_str = NULL;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_put_super(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct super_block *sb;

	sb = cont->op_args.s_put_super.sb;
	sbi = avplg_sbi(sb, cont->plg_id);
	avplg_sbi_free(sbi);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_remount_fs(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct super_block *sb;
	char *opts_in;
	char *opts_out;

	sb = cont->op_args.s_remount_fs.sb;
	sbi = avplg_sbi(sb, cont->plg_id);
	opts_in = cont->op_args.s_remount_fs.opts_in;
	opts_out = cont->op_args.s_remount_fs.opts_out;

	cont->op_rv.rv_int = avplg_set_opts(sbi, opts_in, opts_out);
	if (cont->op_rv.rv_int)
		return PLGFS_STOP;

	mutex_lock(&sbi->mutex);
	avplg_rem_paths(sb, cont->plg_id);
	/* by default include root path "/" */
	if (!sbi->incl_str)
		avplg_set_paths(sb, "/", cont->plg_id, 1);
	avplg_set_paths(sb, sbi->incl_str, cont->plg_id, 1);
	avplg_set_paths(sb, sbi->excl_str, cont->plg_id, 0);
	mutex_unlock(&sbi->mutex);

	kfree(sbi->incl_str);
	kfree(sbi->excl_str);
	sbi->incl_str = sbi->excl_str = NULL;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_show_options(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct super_block *sb;
	struct seq_file *seq; 
	unsigned int msecs;
	unsigned long jiffies;

	seq = cont->op_args.s_show_options.seq;
	sb = cont->op_args.s_show_options.dentry->d_sb;

	sbi = avplg_sbi(sb, cont->plg_id);

	jiffies = avplg_sb_timeout(sbi);

	if (jiffies != MAX_SCHEDULE_TIMEOUT) {
		msecs = jiffies_to_msecs(jiffies);
		seq_printf(seq, ",avplg_timeout=%u", msecs);
	}

	if (!avplg_sb_noclose(sbi))
		seq_printf(seq, ",avplg_close");

	if (avplg_sb_nocache(sbi))
		seq_printf(seq, ",avplg_nocache");

	if (!avplg_sb_nowrite(sbi))
		seq_printf(seq, ",avplg_write");

	mutex_lock(&sbi->mutex);
	avplg_show_paths(sb, cont->plg_id, seq);
	mutex_unlock(&sbi->mutex);

	return PLGFS_CONTINUE;
}

struct avplg_inode_info *avplg_ii(struct inode *i, int id)
{
	return *plgfs_inode_priv(i, id);
}

static enum plgfs_rv avplg_pre_alloc_inode(struct plgfs_context *cont)
{
	struct avplg_inode_info *ii;

	ii = kmem_cache_zalloc(avplg_inode_info_cache, GFP_KERNEL);
	if (!ii) {
		cont->op_rv.rv_inode = NULL;
		return PLGFS_STOP;
	}
	
	spin_lock_init(&ii->lock);
	*plgfs_context_priv(cont, cont->plg_id) = ii;
	atomic_set(&ii->path_info, 0);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_post_alloc_inode(struct plgfs_context *cont)
{
	struct avplg_inode_info *ii;
	struct inode *i;

	i = cont->op_rv.rv_inode;

	ii = *plgfs_context_priv(cont, cont->plg_id);
	if (!ii)
		return PLGFS_CONTINUE;

	if (!i) {
		kmem_cache_free(avplg_inode_info_cache, ii);
		return PLGFS_CONTINUE;
	}

	atomic_set(&ii->path_info, AVPLG_I_NONE);

	*plgfs_inode_priv(i, cont->plg_id) = ii;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_destroy_inode_cb(struct plgfs_context *cont)
{
	struct avplg_inode_info *ii;
	struct inode *i;

	i = cont->op_args.s_destroy_inode_cb.inode;
	ii = avplg_ii(i, cont->plg_id);

	kmem_cache_free(avplg_inode_info_cache, ii);
	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_post_d_instantiate(struct plgfs_context *cont)
{
	struct dentry *d;
	struct inode *i;
	struct avplg_inode_info *ii;
	struct avplg_inode_info *iip;

	d = cont->op_args.d_instantiate.dentry;
	i = cont->op_args.d_instantiate.inode;
	if (!i)
		return PLGFS_CONTINUE;

	iip = avplg_ii(d->d_parent->d_inode, cont->plg_id);
	ii = avplg_ii(i, cont->plg_id);

	atomic_set(&ii->path_info,
			atomic_read(&iip->path_info) & ~AVPLG_I_PATH);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_kill_sb(struct plgfs_context *cont)
{
	struct avplg_sb_info *sbi;
	struct avplg_path *path;
	struct avplg_path *tmp;

	/* Remove all paths here, because we are holding dentry ref. for each
	 * path. This needs to be done before shrink_dcache_for_umount call in
	 * generic_shutdown_super, otherwise we get dentry still in use warning.
	 */
	sbi = avplg_sbi(cont->op_args.t_kill_sb.sb, cont->plg_id);

	list_for_each_entry_safe(path, tmp, &sbi->paths, list) {
		list_del_init(&path->list);
		avplg_path_free(path);
	}

	return PLGFS_CONTINUE;
}

static struct plgfs_op_cbs avplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_TOP_MOUNT].pre = avplg_pre_mount,
	[PLGFS_TOP_MOUNT].post = avplg_post_mount,
	[PLGFS_SOP_SHOW_OPTIONS].pre = avplg_pre_show_options,
	[PLGFS_SOP_REMOUNT_FS].pre = avplg_pre_remount_fs,
	[PLGFS_SOP_PUT_SUPER].pre = avplg_pre_put_super,
	[PLGFS_SOP_ALLOC_INODE].pre = avplg_pre_alloc_inode,
	[PLGFS_SOP_ALLOC_INODE].post = avplg_post_alloc_inode,
	[PLGFS_SOP_DESTROY_INODE_CB].pre = avplg_pre_destroy_inode_cb,
	[PLGFS_REG_FOP_OPEN].pre = avplg_pre_open,
	[PLGFS_REG_FOP_RELEASE].post = avplg_post_release,
	[PLGFS_DOP_D_INSTANTIATE].post = avplg_post_d_instantiate,
	[PLGFS_TOP_KILL_SB].pre = avplg_pre_kill_sb,
};

struct plgfs_plugin avplg = {
	.owner = THIS_MODULE,
	.priority = 850000000,
	.name = "avplg",
	.cbs = avplg_cbs,
	.flags = PLGFS_PLG_HAS_OPTS
};

static int avplg_plgfs_init(void)
{
	return plgfs_register_plugin(&avplg);
}

static void avplg_plgfs_exit(void)
{
	plgfs_unregister_plugin(&avplg);
}

static int avplg_inode_info_init(void)
{
	avplg_inode_info_cache = kmem_cache_create("avplg_inode_info",
			sizeof(struct avplg_inode_info), 0, 0, NULL);

	if (!avplg_inode_info_cache)
		return -ENOMEM;

	return 0;
}

static void avplg_inode_info_exit(void)
{
	rcu_barrier();
	kmem_cache_destroy(avplg_inode_info_cache);
}

static int __init avplg_init(void)
{
	int rv;

	rv = avplg_event_init();
	if (rv)
		return rv;

	rv = avplg_inode_info_init();
	if (rv)
		goto err_inode_info;

	rv = avplg_plgfs_init();
	if (rv) 
		goto err_plgfs;

	rv = avplg_chrdev_init();
	if (rv)
		goto err_chrdev;

	pr_info("anti-virus plugin version "
			AVPLG_VERSION " <www.pluginfs.org>\n");
	return 0;

err_chrdev:
	avplg_plgfs_exit();
err_plgfs:
	avplg_inode_info_exit();
err_inode_info:
	avplg_event_exit();

	return rv;
}

static void __exit avplg_exit(void)
{
	avplg_chrdev_exit();
	avplg_plgfs_exit();
	avplg_inode_info_exit();
	avplg_event_exit();
}

module_init(avplg_init);
module_exit(avplg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frantisek Hrbata <fhrbata@pluginfs.org>");
MODULE_DESCRIPTION("anti-virus plugin for pluginfs");
