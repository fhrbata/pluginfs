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

static DEFINE_MUTEX(plgfs_cache_mutex);
static LIST_HEAD(plgfs_cache_list);

static void plgfs_inode_info_init_once(void *data)
{
	struct plgfs_inode_info *ii = (struct plgfs_inode_info *)data;
	inode_init_once(&ii->vfs_inode);
}

static struct kmem_cache *plgfs_cache_create(const char *fmt, int plg_nr,
		size_t size, size_t align, unsigned long flags,
		void (*ctor)(void *))
{
	struct kmem_cache *cache;
	char *name;

	name = kasprintf(GFP_KERNEL, fmt, plg_nr);
	if (!name)
		return NULL;

	cache = kmem_cache_create(name, size, align, flags, ctor);

	kfree(name);

	if (!cache)
		return NULL;

	return cache;
}

static struct plgfs_cache *plgfs_cache_alloc(int plg_nr)
{
	struct plgfs_cache *cache;
	size_t size;

	size = sizeof(void *) * plg_nr;

	cache = kmalloc(sizeof(struct plgfs_cache), GFP_KERNEL);
	if (!cache)
		goto err;

	cache->fi_cache = plgfs_cache_create("plgfs_file_info_cache_nr%d",
			plg_nr, sizeof(struct plgfs_file_info) + size, 0, 0,
			NULL);

	if (!cache->fi_cache)
		goto err_free_cache;

	cache->di_cache = plgfs_cache_create("plgfs_dentry_info_cache_nr%d",
			plg_nr, sizeof(struct plgfs_dentry_info) + size, 0, 0,
			NULL);

	if (!cache->di_cache)
		goto err_free_fi_cache;

	cache->ii_cache = plgfs_cache_create("plgfs_inode_info_cache_nr%d",
			plg_nr, sizeof(struct plgfs_inode_info) + size, 0, 0,
			plgfs_inode_info_init_once);

	if (!cache->ii_cache)
		goto err_free_di_cache;

	cache->ci_cache = plgfs_cache_create("plgfs_context_info_cache_nr%d",
			plg_nr, sizeof(struct plgfs_context) + size, 0, 0,
			NULL);

	if (!cache->ii_cache)
		goto err_free_ii_cache;

	INIT_LIST_HEAD(&cache->list);
	cache->count = 0;
	cache->plg_nr = plg_nr;

	return cache;

err_free_ii_cache:
	kmem_cache_destroy(cache->ii_cache);
err_free_di_cache:
	kmem_cache_destroy(cache->di_cache);
err_free_fi_cache:
	kmem_cache_destroy(cache->fi_cache);
err_free_cache:
	kfree(cache);
err:
	return ERR_PTR(-ENOMEM);
}

static struct plgfs_cache *plgfs_cache_find(int plg_nr)
{
	struct plgfs_cache *cache;

	list_for_each_entry(cache, &plgfs_cache_list, list) {
		if (cache->plg_nr == plg_nr)
			return cache;
	}

	return NULL;
}

struct plgfs_cache *plgfs_cache_get(int plg_nr)
{
	struct plgfs_cache *cache;

	mutex_lock(&plgfs_cache_mutex);

	cache = plgfs_cache_find(plg_nr);
	if (cache)
		goto found;

	cache = plgfs_cache_alloc(plg_nr);
	if (IS_ERR(cache))
		goto error;

	list_add(&cache->list, &plgfs_cache_list);
found:
	cache->count++;
error:
	mutex_unlock(&plgfs_cache_mutex);
	return cache;
}

void plgfs_cache_put(struct plgfs_cache *cache)
{
	mutex_lock(&plgfs_cache_mutex);

	if (--cache->count) {
		mutex_unlock(&plgfs_cache_mutex);
		return;
	}

	list_del(&cache->list);

	rcu_barrier();

	kmem_cache_destroy(cache->fi_cache);
	kmem_cache_destroy(cache->di_cache);
	kmem_cache_destroy(cache->ii_cache);
	kmem_cache_destroy(cache->ci_cache);
	kfree(cache);

	mutex_unlock(&plgfs_cache_mutex);
}

