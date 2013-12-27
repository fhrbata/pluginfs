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

enum plgfs_options {
	opt_plgs,
	opt_fstype,
	opt_hidden
};

static match_table_t plgfs_tokens = {
	{opt_plgs, "plugins=%s"},
	{opt_fstype, "fstype=%s"},
	{opt_hidden, NULL}
};

void plgfs_pass_on_option(char *opt, char *opts)
{
	if (!opt)
		return;

	if (*opts)
		strcat(opts, ",");

	strcat(opts, opt);
}

static int plgfs_parse_options(char *opts, struct plgfs_mnt_cfg *cfg)
{
	char *opt;
	int token;
	substring_t args[MAX_OPT_ARGS];
	
	if (!opts)
		return 0;

	while ((opt = strsep(&opts, ",")) != NULL) {

		if (!*opt)
			continue;

		token = match_token(opt, plgfs_tokens, args);

		switch (token) {
			case opt_plgs:
				cfg->plgs_str = kstrdup(args[0].from,
						GFP_KERNEL);
				if (!cfg->plgs_str)
					return -ENOMEM;
				break;

			case opt_fstype:
				cfg->fstype_str = kstrdup(args[0].from,
						GFP_KERNEL);
				if (!cfg->fstype_str)
					return -ENOMEM;
				break;

			case opt_hidden:
				plgfs_pass_on_option(opt, cfg->opts);
				break;
		}
	}

	if (!cfg->plgs_str)
		return -EINVAL;

	return 0;
}

static int plgfs_get_plgs_nr(char *plgs)
{
	int nr;

	if (!plgs)
		return 0;

	nr = 1;

	while (*plgs) {
		if (*plgs == ':')
			nr++;
		plgs++;
	}

	return nr;
}

static void plgfs_sort_plgs(struct plgfs_plugin **plgs, int nr)
{
	struct plgfs_plugin *plg;
	int i,j;

	if (nr == 1)
		return;

	/* dummy insert sort */
	for (i = 1, j = 0; i < nr; j = i++) {

		while (j >= 0 && plgs[i]->priority < plgs[j]->priority)
			j--;

		if (i == ++j)
			continue;

		plg = plgs[i];

		memmove(plgs + j + 1, plgs + j,
				sizeof(struct plgfs_plugin *) * (i - j));

		plgs[j] = plg;
	}
}

static int plgfs_get_plgs(char *str, struct plgfs_plugin **plgs, int plgs_nr)
{
	char *plg;
	int nr = 0;

	while ((plg = strsep(&str, ":")) != NULL) {
		if (!*plg)
			continue;

		plgs[nr] = plgfs_get_plg(plg);
		if (!plgs[nr])
			goto error;
		nr++;
	}

	if (nr != plgs_nr)
		goto error;

	plgfs_sort_plgs(plgs, plgs_nr);

	return 0;
error:
	plgfs_put_plgs(plgs, plgs_nr);
	return -EINVAL;
}

struct plgfs_mnt_cfg *plgfs_get_cfg(struct file_system_type *fs_type,
		int flags, const char *dev_name, void *data)
{
	struct plgfs_mnt_cfg *cfg;
	int rv;

	cfg = kzalloc(sizeof(struct plgfs_mnt_cfg), GFP_KERNEL);
	if (!cfg)
		return ERR_PTR(-ENOMEM);

	rv = plgfs_parse_options(data, cfg);
	if (rv)
		goto err;

	cfg->plgs_nr = plgfs_get_plgs_nr(cfg->plgs_str);
	
	cfg->plgs = kzalloc(sizeof(struct plgfs_plugin *) * cfg->plgs_nr,
			GFP_KERNEL);

	rv = -ENOMEM;
	if (!cfg->plgs)
		goto err;

	rv = plgfs_get_plgs(cfg->plgs_str, cfg->plgs, cfg->plgs_nr);
	if (rv)
		goto err;

	plgfs_sort_plgs(cfg->plgs, cfg->plgs_nr);

	if (!dev_name)
		return cfg;

	cfg->opts_orig = data;

	rv = kern_path(dev_name, LOOKUP_FOLLOW, &cfg->path);
	if (rv)
		goto err;

	if (S_ISDIR(cfg->path.dentry->d_inode->i_mode))
		return cfg;

	if (!S_ISBLK(cfg->path.dentry->d_inode->i_mode))
		goto err;

	cfg->mode = FMODE_READ | FMODE_EXCL;

	if (!(flags & MS_RDONLY)) 
		cfg->mode |= FMODE_WRITE;

	cfg->bdev = blkdev_get_by_path(dev_name, cfg->mode, fs_type);
	rv = PTR_ERR(cfg->bdev);
	if (IS_ERR(cfg->bdev))
		goto err;

	return cfg;
err:
	plgfs_put_cfg(cfg);

	return ERR_PTR(rv);
}

struct plgfs_mnt_cfg *plgfs_get_cfg_nodev(int flags, void *data)
{
	return plgfs_get_cfg(NULL, flags, NULL, data);
}

void plgfs_put_cfg(struct plgfs_mnt_cfg *cfg)
{
	if (cfg->bdev)
		blkdev_put(cfg->bdev, cfg->mode);

	if (cfg->plgs)
		plgfs_put_plgs(cfg->plgs, cfg->plgs_nr);

	path_put(&cfg->path);
	kfree(cfg->plgs_str);
	kfree(cfg->fstype_str);
	kfree(cfg->plgs);
	kfree(cfg);
}

int plgfs_show_options(struct seq_file *seq, struct dentry *d)
{
	struct super_block *sbh;
	struct file_system_type *fsth;
	struct plgfs_sb_info *sbi;
	int i;
	int rv = 0;

	sbh = plgfs_dh(d)->d_sb;
	sbi = plgfs_sbi(d->d_sb);
	fsth = sbh->s_type;

	seq_printf(seq, ",fstype=%s", fsth->name);

	seq_printf(seq, ",plugins=%s", sbi->plgs[0]->name);

	for (i = 1; i < sbi->plgs_nr; i++) {
		seq_printf(seq, ":%s", sbi->plgs[i]->name);
	}

	if (sbh->s_op->show_options)
		rv = sbh->s_op->show_options(seq, plgfs_dh(d));

	return rv;
}

EXPORT_SYMBOL(plgfs_pass_on_option);
