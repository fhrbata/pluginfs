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

#include <pluginfs.h>
#include <linux/parser.h>
#include <linux/blkdev.h>
#include <linux/genhd.h>
#include <linux/seq_file.h>

static enum plgfs_rv miniplg_open(struct plgfs_context *cont)
{
	char *buf;
	char *fn;
	char *call;
	char *mode;

	buf = (char *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (!buf) {
		cont->op_rv.rv_int = -ENOMEM;
		return PLGFS_STOP;
	}

	fn = d_path(&cont->op_args.f_open.file->f_path, buf, PAGE_SIZE);
	if (IS_ERR(fn)) {
		free_page((unsigned long)buf);
		cont->op_rv.rv_int = PTR_ERR(fn);
		return PLGFS_STOP;
	}

	call = (cont->op_call == PLGFS_PRECALL) ? "pre" : "post";

	switch (cont->op_id) {
		case PLGFS_REG_FOP_OPEN:
			mode = "reg";
			break;

		case PLGFS_DIR_FOP_OPEN:
			mode = "dir";
			break;

		default:
			mode = "unk";
	}

	pr_info("miniplg: %s open %s %s\n", call, mode, fn);

	free_page((unsigned long)fn);

	return PLGFS_CONTINUE;
}

enum plgfs_options {
	opt_miniplg,
	opt_unknown
};

static match_table_t miniplg_tokens = {
	{opt_miniplg, "miniplg=%s"},
	{opt_unknown, NULL}
};

static enum plgfs_rv miniplg_pre_mount(struct plgfs_context *cont)
{
	char *opt;
	int token;
	substring_t args[MAX_OPT_ARGS];
	char *opts_in;
	char *opts_out;
	char *miniplg_opt;

	miniplg_opt = NULL;
	opts_in = cont->op_args.t_mount.opts_in;
	opts_out = cont->op_args.t_mount.opts_out;

	if (cont->op_args.t_mount.bdev)
		pr_info("miniplg: pre mount: using block device %s\n",
				cont->op_args.t_mount.bdev->bd_disk->disk_name);

	if (!opts_in)
		return PLGFS_CONTINUE;

	while ((opt = strsep(&opts_in, ",")) != NULL) {

		if (!*opt)
			continue;

		token = match_token(opt, miniplg_tokens, args);

		switch (token) {
			case opt_miniplg:
				miniplg_opt = kstrdup(args[0].from, GFP_KERNEL);
				if (!miniplg_opt) {
					cont->op_rv.rv_int = -ENOMEM;
					return PLGFS_STOP;
				}

				pr_info("miniplg: pre mount: option "
						"miniplg=%s\n", miniplg_opt);
				break;

			case opt_unknown:
				plgfs_pass_on_option(opt, opts_out);
				break;
		}
	}

	if (!miniplg_opt)
		return PLGFS_CONTINUE;

	*plgfs_sb_priv(cont->op_args.t_mount.sb, cont->plg_id) =  miniplg_opt;

	return PLGFS_CONTINUE;
}

static enum plgfs_rv miniplg_post_mount(struct plgfs_context *cont)
{
	char *miniplg_opt;
	struct super_block *sb;
	struct path *path;

	sb = cont->op_args.t_mount.sb;
	path = cont->op_args.t_mount.path;

	if (cont->op_rv.rv_int) {
		miniplg_opt = *plgfs_sb_priv(sb, cont->plg_id);
		*plgfs_sb_priv(sb, cont->plg_id) = NULL;
		kfree(miniplg_opt);
		return PLGFS_CONTINUE;
	}

	pr_info("miniplg: post mount: file system type: %s\n",
			path->dentry->d_sb->s_type->name);

	return PLGFS_CONTINUE;
}

static enum plgfs_rv miniplg_pre_put_super(struct plgfs_context *cont)
{
	char *miniplg_opt;
	struct super_block *sb;

	sb = cont->op_args.s_put_super.sb;
	miniplg_opt = *plgfs_sb_priv(sb, cont->plg_id);
	kfree(miniplg_opt);
	return PLGFS_CONTINUE;
}

static enum plgfs_rv miniplg_pre_show_options(struct plgfs_context *cont)
{
	struct super_block *sb;
	struct seq_file *seq; 
	char *miniplg_opt;

	seq = cont->op_args.s_show_options.seq;
	sb = cont->op_args.s_show_options.dentry->d_sb;

	miniplg_opt = *plgfs_sb_priv(sb, cont->plg_id);
	if (!miniplg_opt)
		return PLGFS_CONTINUE;

	seq_printf(seq, ",miniplg=%s", miniplg_opt);

	return PLGFS_CONTINUE;
}

static struct plgfs_op_cbs miniplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_TOP_MOUNT].pre = miniplg_pre_mount,
	[PLGFS_TOP_MOUNT].post = miniplg_post_mount,
	[PLGFS_SOP_PUT_SUPER].pre = miniplg_pre_put_super,
	[PLGFS_SOP_SHOW_OPTIONS].pre = miniplg_pre_show_options,
	[PLGFS_REG_FOP_OPEN].pre = miniplg_open,
	[PLGFS_REG_FOP_OPEN].post = miniplg_open,
	[PLGFS_DIR_FOP_OPEN].pre = miniplg_open,
	[PLGFS_DIR_FOP_OPEN].post = miniplg_open,
};

static struct plgfs_plugin miniplg = {
	.owner = THIS_MODULE,
	.priority = 1,
	.name = "miniplg",
	.cbs = miniplg_cbs,
	.flags = PLGFS_PLG_HAS_OPTS
};

static int __init miniplg_init(void)
{
	return plgfs_register_plugin(&miniplg);
}

static void __exit miniplg_exit(void)
{
	plgfs_unregister_plugin(&miniplg);
}

module_init(miniplg_init);
module_exit(miniplg_exit);

MODULE_LICENSE("GPL");
