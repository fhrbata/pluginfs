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

int plgfs_walk_dtree(struct plgfs_plugin *plg, struct dentry *root,
		int (*cb)(struct dentry *, void *, int), void *data)
{
	LIST_HEAD(walk);
	struct plgfs_sb_info *sbi;
	struct plgfs_dentry_info *tmp;
	struct plgfs_dentry_info *di; /* dentry info */
	struct dentry *d;             /* dentry/child */
	struct dentry *dp;            /* dentry parent */
	int id;                       /* plugin id for sb */
	int rv;

	if (root->d_sb->s_magic != PLGFS_MAGIC)
		return -EINVAL;

	id = plgfs_get_plugin_sb_id(plg, root->d_sb);
	if (id < 0)
		return -EINVAL;

	rv = 0;
	sbi = plgfs_sbi(root->d_sb);

	/* only one dcache walk per sb */
	mutex_lock(&sbi->mutex_walk);

	di = plgfs_di(root);
	di->dentry_walk = dget(root);
	list_add_tail(&di->list_walk, &walk);

	while (!list_empty(&walk)) {
		di = list_entry(walk.next, struct plgfs_dentry_info,
				list_walk);
		dp = di->dentry_walk;

		rv = cb(dp, data, id);
		if (rv > 0)
			goto skip;

		if (rv < 0)
			goto error;

		spin_lock(&dp->d_lock);

		list_for_each_entry(d, &dp->d_subdirs, d_u.d_child) {
			spin_lock_nested(&d->d_lock, DENTRY_D_LOCK_NESTED);
			dget_dlock(d);
			spin_unlock(&d->d_lock);
			di = plgfs_di(d);
			di->dentry_walk = d;
			list_add_tail(&di->list_walk, &walk);
		}

		spin_unlock(&dp->d_lock);
skip:
		di = plgfs_di(dp);
		list_del_init(&di->list_walk);
		dput(di->dentry_walk);
	}
error:
	list_for_each_entry_safe(di, tmp, &walk, list_walk) {
		list_del_init(&di->list_walk);
		dput(di->dentry_walk);
	}

	mutex_unlock(&sbi->mutex_walk);

	return rv;
}

static void plgfs_d_release(struct dentry *d)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct plgfs_dentry_info *dpi; /* dentry parent info */
	struct plgfs_dentry_info *di; /* dentry info */

	di = plgfs_di(d);
	dpi = plgfs_di(d->d_parent);
	sbi = plgfs_sbi(d->d_sb);
	cont = plgfs_alloc_context(sbi);
	if (IS_ERR(cont)) {
		/* try to at least free the resources*/
		if (!IS_ROOT(d))
			dput(plgfs_dh(d));
		kmem_cache_free(sbi->cache->di_cache, di);
		pr_err("pluginfs: cannot alloc context for dentry release, no"
				"plugins will be called\n");
		return;
	}

	cont->op_id = PLGFS_DOP_D_RELEASE,
	cont->op_args.d_release.dentry = d;

	plgfs_precall_plgs(cont, sbi);

	dput(plgfs_dh(d));

	plgfs_postcall_plgs(cont, sbi);

	kmem_cache_free(sbi->cache->di_cache, di);

	plgfs_free_context(sbi, cont);
}

static int plgfs_d_revalidate(struct dentry *d, unsigned int flags)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(d->d_sb);
	cont = plgfs_alloc_context_atomic(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DOP_D_REVALIDATE;
	cont->op_args.d_revalidate.dentry = d;
	cont->op_args.d_revalidate.flags = flags;

	if(!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.d_revalidate.dentry;
	flags = cont->op_args.d_revalidate.flags;

	dh = plgfs_dh(d);
	cont->op_rv.rv_int = 1;
	if (!(dh->d_flags & DCACHE_OP_REVALIDATE))
		goto postcalls;

	cont->op_rv.rv_int = dh->d_op->d_revalidate(dh, flags);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_d_hash(const struct dentry *d, struct qstr *s)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	struct dentry *dh;
	int rv;

	sbi = plgfs_sbi(d->d_sb);
	cont = plgfs_alloc_context_atomic(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DOP_D_HASH;
	cont->op_args.d_hash.dentry = d;
	cont->op_args.d_hash.str = s;

	if(!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	d = cont->op_args.d_hash.dentry;
	s = cont->op_args.d_hash.str;

	dh = plgfs_dh((struct dentry *)d);
	cont->op_rv.rv_int = 0;
	if (!(dh->d_flags & DCACHE_OP_HASH))
		goto postcalls;

	cont->op_rv.rv_int = dh->d_op->d_hash(dh, s);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

static int plgfs_d_compare(const const struct dentry *dp,
		const struct dentry *d, unsigned int len, const const char *str,
		const struct qstr *name)
{
	struct plgfs_context *cont;
	struct plgfs_sb_info *sbi;
	const struct dentry *dh;
	const struct dentry *dph;
	int rv;

	sbi = plgfs_sbi(d->d_sb);
	cont = plgfs_alloc_context_atomic(sbi);
	if (IS_ERR(cont))
		return PTR_ERR(cont);

	cont->op_id = PLGFS_DOP_D_COMPARE;
	cont->op_args.d_compare.parent = dp;
	cont->op_args.d_compare.dentry = d;
	cont->op_args.d_compare.len = len;
	cont->op_args.d_compare.str = str;
	cont->op_args.d_compare.name = name;

	if(!plgfs_precall_plgs(cont, sbi))
		goto postcalls;

	dp = cont->op_args.d_compare.parent;
	d = cont->op_args.d_compare.dentry;
	len = cont->op_args.d_compare.len;
	str = cont->op_args.d_compare.str;
	name = cont->op_args.d_compare.name;

	dph = plgfs_dh((struct dentry *)dp);
	dh = plgfs_dh((struct dentry *)d);

	if (!(dh->d_flags & DCACHE_OP_COMPARE)) {
		cont->op_rv.rv_int = strcmp(str, name->name);
		goto postcalls;
	}

	cont->op_rv.rv_int = dh->d_op->d_compare(dph, dh, len, str,
			name);

postcalls:
	plgfs_postcall_plgs(cont, sbi);

	rv = cont->op_rv.rv_int;

	plgfs_free_context(sbi, cont);

	return rv;
}

const struct dentry_operations plgfs_dops = {
	.d_release = plgfs_d_release,
	.d_revalidate = plgfs_d_revalidate,
	.d_hash = plgfs_d_hash,
	.d_compare = plgfs_d_compare,
};

struct plgfs_dentry_info *plgfs_alloc_di(struct dentry *d)
{
	struct plgfs_sb_info *sbi;
	struct plgfs_dentry_info *di;

	sbi = plgfs_sbi(d->d_sb);

	di = kmem_cache_zalloc(sbi->cache->di_cache, GFP_KERNEL);
	if (!di)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&di->list_walk);

	return di;
}
