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

static enum plgfs_rv avplg_check_file(struct file *file, int type,
		struct plgfs_context *cont)
{
	int rv;

	if (!avplg_should_check(file))
		return PLGFS_CONTINUE;

	rv = avplg_event_process(file, type);

	return avplg_eval_res(rv, cont);
}

static enum plgfs_rv avplg_pre_open(struct plgfs_context *cont)
{
	struct file *file;

	file = cont->op_args.f_open.file;

	return avplg_check_file(file, AVPLG_EVENT_OPEN, cont);
}

static enum plgfs_rv avplg_post_release(struct plgfs_context *cont)
{
	struct file *file;

	file = cont->op_args.f_release.file;

	return avplg_check_file(file, AVPLG_EVENT_CLOSE, cont);
}

enum avplg_options {
	opt_timeout,
	opt_close,
	opt_cache,
	opt_noclose,
	opt_nocache,
	opt_unknown
};

static match_table_t avplg_tokens = {
	{opt_timeout, "avplg_timeout=%u"},
	{opt_close, "avplg_close"},
	{opt_cache, "avplg_cache"},
	{opt_noclose, "avplg_noclose"},
	{opt_nocache, "avplg_nocache"},
	{opt_unknown, NULL}
};

static void avplg_set_flags(struct avplg_sb_info *asbi, unsigned int flags)
{
	asbi->flags = flags;
	wmb();
}

static unsigned int avplg_get_flags(struct avplg_sb_info *asbi)
{
	rmb();
	return asbi->flags;
}

unsigned int avplg_get_noclose(struct avplg_sb_info *asbi)
{
	return avplg_get_flags(asbi) & AVPLG_NOCLOSE;
}

unsigned int avplg_get_nocache(struct avplg_sb_info *asbi)
{
	return avplg_get_flags(asbi) & AVPLG_NOCACHE;
}

static void avplg_set_timeout(struct avplg_sb_info *asbi, unsigned long timeout)
{
	asbi->jiffies = timeout;
	wmb();
}

unsigned long avplg_get_timeout(struct avplg_sb_info *asbi)
{
	rmb();
	return asbi->jiffies;
}

static int avplg_set_opts(struct avplg_sb_info *asbi, char *opts_in,
		char *opts_out)
{
	substring_t args[MAX_OPT_ARGS];
	char *opt;
	int token;
	unsigned int flags;
	unsigned int timeout;
	unsigned long jiffies;

	flags = 0;
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

			case opt_noclose:
				flags |= AVPLG_NOCLOSE;
				break;

			case opt_nocache:
				flags |= AVPLG_NOCACHE;
				break;

			case opt_unknown:
				plgfs_pass_on_option(opt, opts_out);
				break;
		}
	}

	avplg_set_flags(asbi, flags);
	avplg_set_timeout(asbi, jiffies);

	return 0;
}

static enum plgfs_rv avplg_pre_mount(struct plgfs_context *cont)
{
	struct avplg_sb_info *asbi;
	char *opts_in;
	char *opts_out;

	asbi = kzalloc(sizeof(struct avplg_sb_info), GFP_KERNEL);
	if (!asbi) {
		cont->op_rv.rv_int = -ENOMEM;
		return PLGFS_STOP;
	}

	opts_in = cont->op_args.t_mount.opts_in;
	opts_out = cont->op_args.t_mount.opts_out;

	cont->op_rv.rv_int = avplg_set_opts(asbi, opts_in, opts_out);
	if (cont->op_rv.rv_int) {
		kfree(asbi);
		return PLGFS_STOP;
	}

	plgfs_set_sb_priv(cont->op_args.t_mount.sb, cont->plg_id, asbi);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_post_mount(struct plgfs_context *cont)
{
	struct avplg_sb_info *asbi;
	struct super_block *sb;

	sb = cont->op_args.t_mount.sb;
	asbi = plgfs_get_sb_priv(sb, cont->plg_id);
	if (cont->op_rv.rv_int)
		kfree(asbi);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_put_super(struct plgfs_context *cont)
{
	struct avplg_sb_info *asbi;
	struct super_block *sb;

	sb = cont->op_args.s_put_super.sb;
	asbi = plgfs_get_sb_priv(sb, cont->plg_id);
	kfree(asbi);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_remount_fs(struct plgfs_context *cont)
{
	struct avplg_sb_info *asbi;
	struct super_block *sb;
	char *opts_in;
	char *opts_out;

	sb = cont->op_args.s_remount_fs.sb;
	asbi = plgfs_get_sb_priv(sb, cont->plg_id);
	opts_in = cont->op_args.s_remount_fs.opts_in;
	opts_out = cont->op_args.s_remount_fs.opts_out;

	cont->op_rv.rv_int = avplg_set_opts(asbi, opts_in, opts_out);
	if (cont->op_rv.rv_int)
		return PLGFS_STOP;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv avplg_pre_show_options(struct plgfs_context *cont)
{
	struct avplg_sb_info *asbi;
	struct super_block *sb;
	struct seq_file *seq; 
	unsigned int val;

	seq = cont->op_args.s_show_options.seq;
	sb = cont->op_args.s_show_options.dentry->d_sb;

	asbi = plgfs_get_sb_priv(sb, cont->plg_id);

	val = jiffies_to_msecs(avplg_get_timeout(asbi));
	seq_printf(seq, ",avplg_timeout=%u", val);

	if (avplg_get_noclose(asbi))
		seq_printf(seq, ",avplg_noclose");

	if (avplg_get_nocache(asbi))
		seq_printf(seq, ",avplg_nocache");

	return PLGFS_CONTINUE;
}

static struct plgfs_op_cbs avplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_TOP_MOUNT].pre = avplg_pre_mount,
	[PLGFS_TOP_MOUNT].post = avplg_post_mount,
	[PLGFS_SOP_SHOW_OPTIONS].pre = avplg_pre_show_options,
	[PLGFS_SOP_REMOUNT_FS].pre = avplg_pre_remount_fs,
	[PLGFS_SOP_PUT_SUPER].pre = avplg_pre_put_super,
	[PLGFS_REG_FOP_OPEN].pre = avplg_pre_open,
	[PLGFS_REG_FOP_RELEASE].post = avplg_post_release,
};

static struct plgfs_plugin avplg = {
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

static int __init avplg_init(void)
{
	int rv;

	rv = avplg_event_init();
	if (rv)
		return rv;

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
	avplg_event_exit();

	return rv;
}

static void __exit avplg_exit(void)
{
	avplg_chrdev_exit();
	avplg_plgfs_exit();
	avplg_event_exit();
}

module_init(avplg_init);
module_exit(avplg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Frantisek Hrbata <fhrbata@pluginfs.org>");
MODULE_DESCRIPTION("anti-virus plugin for pluginfs");
