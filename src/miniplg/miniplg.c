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

static struct plgfs_op_cbs miniplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_REG_FOP_OPEN].pre = miniplg_open,
	[PLGFS_REG_FOP_OPEN].post = miniplg_open,
	[PLGFS_DIR_FOP_OPEN].pre = miniplg_open,
	[PLGFS_DIR_FOP_OPEN].post = miniplg_open,
};

static struct plgfs_plugin miniplg = {
	.owner = THIS_MODULE,
	.priority = 1,
	.name = "miniplg",
	.cbs = miniplg_cbs
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
