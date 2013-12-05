#include <pluginfs.h>

#define MULTIPLG_NR 10
#define MULTIPLG_PRIO 12345
#define MULTIPLG_NAME "multiplg"
#define MULTIPLG_NAME_SIZE 16

struct multiplg_plugin {
	struct plgfs_plugin plg;
	char name[MULTIPLG_NAME_SIZE];
};

static struct multiplg_plugin multiplgs[MULTIPLG_NR];

static enum plgfs_rv multiplg_open(struct plgfs_context *cont)
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

	pr_info("%s: %s open %s %s\n", cont->plg->name, call, mode, fn);

	free_page((unsigned long)fn);

	return PLGFS_CONTINUE;
}

static struct plgfs_op_cbs multiplg_cbs[PLGFS_OP_NR] = {
	[PLGFS_REG_FOP_OPEN].pre = multiplg_open,
	[PLGFS_REG_FOP_OPEN].post = multiplg_open,
	[PLGFS_DIR_FOP_OPEN].pre = multiplg_open,
	[PLGFS_DIR_FOP_OPEN].post = multiplg_open,
};

static struct plgfs_plugin multiplg = {
	.owner = THIS_MODULE,
	.priority = MULTIPLG_PRIO,
	.name = MULTIPLG_NAME,
	.cbs = multiplg_cbs
};

static int __init multiplg_reg_plgs(void)
{
	struct plgfs_plugin *plg;
	int rv;
	int nr;
	int i;

	nr = 0;

	for (i = 0; i < MULTIPLG_NR; i++) {
		plg = &multiplgs[i].plg;
		plg->name = multiplgs[i].name;

		plg->owner = THIS_MODULE;
		plg->priority = MULTIPLG_PRIO + i + 1;
		plg->cbs = multiplg_cbs;
		snprintf(plg->name, MULTIPLG_NAME_SIZE, "%s_%d",
				MULTIPLG_NAME, i + 1);

		rv = plgfs_register_plugin(plg);
		if (rv)
			goto err;
		nr++;
	}

	return 0;
err:
	for (i = 0; i < nr; i++) {
		plgfs_unregister_plugin(&multiplgs[i].plg);
	}

	return rv;
}

static void __exit multiplg_unreg_plgs(void)
{
	int i;

	for (i = 0; i < MULTIPLG_NR; i++) {
		plgfs_unregister_plugin(&multiplgs[i].plg);
	}
}

static int __init multiplg_init(void)
{
	int rv;

	rv = plgfs_register_plugin(&multiplg);
	if (rv)
		return rv;

	if (!MULTIPLG_NR)
		return 0;

	rv = multiplg_reg_plgs();
	if (rv)
		plgfs_unregister_plugin(&multiplg);

	return rv;
}

static void __exit multiplg_exit(void)
{
	plgfs_unregister_plugin(&multiplg);

	if (!MULTIPLG_NR)
		return;

	multiplg_unreg_plgs();
}

module_init(multiplg_init);
module_exit(multiplg_exit);

MODULE_LICENSE("GPL");
