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

static struct plgfs_op_cbs avplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_REG_FOP_OPEN].pre = avplg_pre_open,
	[PLGFS_REG_FOP_RELEASE].post = avplg_post_release,
};

static struct plgfs_plugin avplg = {
	.owner = THIS_MODULE,
	.priority = 850000000,
	.name = "avplg",
	.cbs = avplg_cbs
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
