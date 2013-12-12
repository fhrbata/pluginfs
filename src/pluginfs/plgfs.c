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

int plgfs_precall_plgs(struct plgfs_context *cont, struct plgfs_sb_info *sbi)
{
	struct plgfs_plugin *plg;
	enum plgfs_rv rv;
	int idx;

	cont->op_call = PLGFS_PRECALL;

	for (idx = cont->idx_start; idx < sbi->plgs_nr; idx++) {
		plg = sbi->plgs[idx];

		cont->plg = plg;
		cont->plg_id = idx;

		if (!plg->cbs[cont->op_id].pre)
			continue;

		rv = plg->cbs[cont->op_id].pre(cont);

		if (rv == PLGFS_STOP) {
			cont->idx_end = idx;
			return 0;
		}
	}

	cont->idx_end = idx - 1;

	return 1;
}

void plgfs_postcall_plgs(struct plgfs_context *cont, struct plgfs_sb_info *sbi)
{
	struct plgfs_plugin *plg;
	int idx;

	cont->op_call = PLGFS_POSTCALL;

	for (idx = cont->idx_end; idx >= cont->idx_start; idx--) {
		plg = sbi->plgs[idx];

		cont->plg = plg;
		cont->plg_id = idx;

		if (!plg->cbs[cont->op_id].post)
			continue;

		plg->cbs[cont->op_id].post(cont);
	}
}

struct plgfs_context *plgfs_alloc_context_atomic(struct plgfs_sb_info *sbi)
{
	struct plgfs_context *cont;

	cont = kmem_cache_zalloc(sbi->cache->ci_cache, GFP_ATOMIC);
	if (!cont)
		return ERR_PTR(-ENOMEM);

	return cont;
}

struct plgfs_context *plgfs_alloc_context(struct plgfs_sb_info *sbi)
{
	struct plgfs_context *cont;

	cont = kmem_cache_zalloc(sbi->cache->ci_cache, GFP_KERNEL);
	if (!cont)
		return ERR_PTR(-ENOMEM);

	return cont;
}

void plgfs_free_context(struct plgfs_sb_info *sbi, struct plgfs_context *cont)
{
	kmem_cache_free(sbi->cache->ci_cache, cont);
}

static int plgfs_test_super(struct super_block *sb, void *data)
{
	struct plgfs_mnt_cfg *cfg;
	struct plgfs_sb_info *sbi;
	int i;

	cfg = (struct plgfs_mnt_cfg *)data;
	sbi = plgfs_sbi(sb);

	if (sbi->pdev->bdev_hidden != cfg->bdev)
		return 0;

	cfg->flags |= PLGFS_OPT_DIFF_PLGS;

	if (sbi->plgs_nr != cfg->plgs_nr)
		return 0;

	for (i = 0; i < cfg->plgs_nr; i++) {
		if (sbi->plgs[i] != cfg->plgs[i])
			return 0;
	}

	cfg->flags &= ~PLGFS_OPT_DIFF_PLGS;

	return 1;
}

static struct dentry *plgfs_mount(struct file_system_type *fs_type, int flags,
		const char *dev_name, void *data_page)
{
	struct super_block *sb;
	struct plgfs_mnt_cfg *cfg;
	int rv;

	cfg = plgfs_get_cfg(fs_type, flags, dev_name, data_page);
	if (IS_ERR(cfg))
		return ERR_CAST(cfg);

	sb = sget(fs_type, plgfs_test_super, set_anon_super, flags, cfg);
	if (IS_ERR(sb)) {
		plgfs_put_cfg(cfg);
		return ERR_CAST(sb);
	}

	if (sb->s_root) {
		plgfs_put_cfg(cfg);
		return dget(sb->s_root); 
	}

	if (cfg->flags & PLGFS_OPT_DIFF_PLGS) {
		pr_err("pluginfs: \"%s\" already mounted with different set of "
				"plugins\n", dev_name);
		deactivate_locked_super(sb);
		plgfs_put_cfg(cfg);
		return ERR_PTR(-EINVAL);
	}

	rv = plgfs_fill_super(sb, flags, cfg);
	if (rv) {
		deactivate_locked_super(sb);
		plgfs_put_cfg(cfg);
		return ERR_PTR(rv);
	}

	plgfs_put_cfg(cfg);

	return dget(sb->s_root); 
}

static void plgfs_kill_sb(struct super_block *sb)
{
	kill_anon_super(sb);
}

struct file_system_type plgfs_type = {
	.owner = THIS_MODULE,
	.name = "pluginfs",
	.mount = plgfs_mount,
	.kill_sb = plgfs_kill_sb,
	.fs_flags = 0
};

static int __init plgfs_init(void)
{
	int rv;

	plgfs_major = register_blkdev(0, "pluginfs");
	if (plgfs_major < 0)
	       return plgfs_major;	

	rv = register_filesystem(&plgfs_type);
	if (rv) {
		unregister_blkdev(plgfs_major, "pluginfs");
		return rv;
	}

	pr_info("Plugin File System Version " PLGFS_VERSION); 

	return 0;
}

static void __exit plgfs_exit(void)
{
	unregister_blkdev(plgfs_major, "pluginfs");
	unregister_filesystem(&plgfs_type);
}

module_init(plgfs_init);
module_exit(plgfs_exit);

MODULE_DESCRIPTION("Plugin File System Version " PLGFS_VERSION);
MODULE_LICENSE("GPL");
