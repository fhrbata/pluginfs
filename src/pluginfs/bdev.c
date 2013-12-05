#include "plgfs.h"

int plgfs_major;
static DEFINE_SPINLOCK(plgfs_dev_ida_lock);
static DEFINE_IDA(plgfs_dev_ida);

static int plgfs_alloc_dev_minor(void)
{
	int minor;
	int rv;
again:
	spin_lock(&plgfs_dev_ida_lock);
	rv = ida_get_new_above(&plgfs_dev_ida, 0, &minor);
	spin_unlock(&plgfs_dev_ida_lock);

	if (!rv) 
		return minor;

	if (rv != -EAGAIN)
		return rv;

	if (ida_pre_get(&plgfs_dev_ida, GFP_KERNEL))
		goto again;

	return -ENOMEM;
}

static void plgfs_free_dev_minor(int minor)
{
	spin_lock(&plgfs_dev_ida_lock);
	ida_remove(&plgfs_dev_ida, minor);
	spin_unlock(&plgfs_dev_ida_lock);
}

static void plgfs_end_bio(struct bio *bioh, int err)
{
	struct bio *bio;

	bio = (struct bio *)bioh->bi_private;
	kfree(bioh);

	bio_endio(bio, err);
}

static void plgfs_make_request(struct request_queue *q, struct bio *bio)
{
	struct plgfs_dev *pdev;
	struct bio *bioh; /* bio hidden */


	bioh = bio_clone(bio, GFP_NOIO);
	if (!bioh) {
		bio_endio(bio, -ENOMEM);
		return;
	}

	pdev = (struct plgfs_dev *)q->queuedata;

	bioh->bi_bdev = pdev->bdev_hidden;
	bioh->bi_end_io = plgfs_end_bio;
	bioh->bi_private = bio;

	generic_make_request(bioh);
}

static int plgfs_bdev_open(struct block_device *bd, fmode_t mode)
{
	struct plgfs_dev *pdev;
	
	pdev = bd->bd_disk->private_data;
	
	if (!(mode & FMODE_EXCL))
		return 0;
	
	/* Prevent someone else than pluginfs to mess with pluginfs' stackable
	 * bdev. This bdev is used internally by pluginfs to mount the hidden
	 * regular file system. Use the original bdev to mount pluginfs on
	 * another location, where the plugin set is checked also.
	 */
	if (pdev->count) {
		pr_err("pluginfs: \"%s\" is already exclusively used, do not "
				"try to mount it manually\n",
				pdev->gd->disk_name);
		return -EBUSY;
	}

	pdev->count++;
	
	return 0;
}

static void plgfs_bdev_release(struct gendisk *gd, fmode_t mode)
{
	struct plgfs_dev *pdev;

	pdev = gd->private_data;
	
	if (!(mode & FMODE_EXCL))
		return;

	pdev->count--;
}

static const struct block_device_operations plgfs_bdev_fops = {
	.owner = THIS_MODULE,
	.open = plgfs_bdev_open,
	.release = plgfs_bdev_release
};

struct plgfs_dev *plgfs_add_dev(struct block_device *bdev, fmode_t mode)
{
	struct plgfs_dev *pdev;
	int err;
	
	pdev = kzalloc(sizeof(struct plgfs_dev), GFP_KERNEL);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	err = -ENOMEM;

	pdev->gd = alloc_disk(1);
	if (!pdev->gd)
		goto err_free_pdev;

	pdev->queue = blk_alloc_queue(GFP_KERNEL);
	if (!pdev->queue)
		goto err_free_disk;

	err = pdev->minor = plgfs_alloc_dev_minor();
	if (pdev->minor < 0)
		goto err_free_queue;

	/* this should never fail, we grabbed the bdev in plgfs_get_cfg */
	BUG_ON(blkdev_get(bdev, mode, &plgfs_type));
	pdev->bdev_hidden = bdev;
	pdev->mode = mode;
	pdev->gd->fops = &plgfs_bdev_fops;
	pdev->gd->major = plgfs_major;
	pdev->gd->private_data = pdev;
	pdev->gd->flags |= GENHD_FL_NO_PART_SCAN;
	pdev->gd->first_minor = pdev->minor;
	pdev->queue->queuedata = pdev;
	pdev->gd->queue = pdev->queue;
	sprintf(pdev->gd->disk_name, "pluginfs%d", pdev->gd->first_minor);
	blk_set_stacking_limits(&pdev->queue->limits);
	pdev->queue->limits.logical_block_size = bdev_logical_block_size(bdev);
	blk_queue_make_request(pdev->queue, plgfs_make_request);
	set_capacity(pdev->gd, bdev->bd_part->nr_sects);

	add_disk(pdev->gd);

	pdev->bdev = bdget_disk(pdev->gd, 0);

	return pdev;

err_free_queue:
	blk_cleanup_queue(pdev->queue);
err_free_disk:
	put_disk(pdev->gd);
err_free_pdev:
	kfree(pdev);

	return ERR_PTR(err);
}

void plgfs_rem_dev(struct plgfs_dev *pdev)
{
	del_gendisk(pdev->gd);
	blk_cleanup_queue(pdev->queue);
	put_disk(pdev->gd);
	plgfs_free_dev_minor(pdev->minor);
	blkdev_put(pdev->bdev_hidden, pdev->mode);
	kfree(pdev);
}
