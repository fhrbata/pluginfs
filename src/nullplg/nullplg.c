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

static struct plgfs_op_cbs nullplg_cbs[PLGFS_OP_NR] = {};

static struct plgfs_plugin nullplg = {
	.owner = THIS_MODULE,
	.priority = 0,
	.name = "nullplg",
	.cbs = nullplg_cbs
};

static int __init nullplg_init(void)
{
	return plgfs_register_plugin(&nullplg);
}

static void __exit nullplg_exit(void)
{
	plgfs_unregister_plugin(&nullplg);
}

module_init(nullplg_init);
module_exit(nullplg_exit);

MODULE_LICENSE("GPL");
