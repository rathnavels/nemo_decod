#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>	// v4l2 framework level device
#include <media/v4l2-dev.h>	// video_device that becomes /dev/videoX
#include <media/v4l2-mem2mem.h>

#define NEMO_CAPS (V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING)

/* 
 * A platform device is something that is not enumerated automatically.
 * It generally is defined in the device tree source.
 * A platform driver is the software knob of the device.
 * Platform driver struct has the definition for probe, remove etc.
 * 
 * Apparently drivers can be platform drivers which define a platform device with probe etc,
 * or simple software drivers with module init and exit.
 * 
 * 
 */

// nemo_dev - Stores handlers for the whole device
struct nemo_dev {
	struct v4l2_device v4l2_dev;	// v4l2 device 
	struct video_device vid_dev;	// video device -> /dev/videoX
	struct v4l2_m2m_dev *m2m_dev;   // m2m_dev scheduler to be shared by whole device.
	struct mutex lock;
};

/* 
 * nemo_ctx - Stores handlers for a single instance/session
 * Each open file will get its own nemo_ctx and m2m queue pair.
 */
struct nemo_ctx {
	struct v4l2_fh fh;
	struct nemo_dev *dev;
	struct v4l2_m2m_ctx *m2m_ctx;
	struct v4l2_pix_format_mplane out_fmt;
	struct v4l2_pix_format_mplane cap_fmt;
};

static struct nemo_dev *g_nemo;

static void nemo_fill_format(struct v4l2_pix_format_mplane *pix_mp, u32 width, u32 height)
{
	memset(pix_mp, 0, sizeof(*pix_mp));

	pix_mp->width = clamp_t(u32, width, 64, 1920) & ~1U; // ~1U makes it even number
	pix_mp->height = clamp_t(u32, height, 64, 1080) & ~1U;
	pix_mp->pixelformat = V4L2_PIX_FMT_NV12M;
	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->num_planes = 2;

	for (int i = 0; i < 2; i++)
	{
		pix_mp->plane_fmt[i].bytesperline = pix_mp->width;
		pix_mp->plane_fmt[i].sizeimage = pix_mp->width * pix_mp->height / ((i & 1) ? 2 : 1);
	}
}

static struct v4l2_pix_format_mplane* nemo_get_queue_format(struct nemo_ctx *ctx, enum v4l2_buf_type type)
{
	switch (type)
	{
		case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
			return &ctx->out_fmt;
		case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
			return &ctx->cap_fmt;
		default:
			return NULL;
	}
}

static int nemo_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE || f->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	nemo_fill_format(&f->fmt.pix_mp, f->fmt.pix_mp.width, f->fmt.pix_mp.height);

	return 0;
}

static int nemo_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct nemo_ctx *ctx = container_of(priv, struct nemo_ctx, fh);
	struct v4l2_pix_format_mplane *pix_mp;

	pix_mp = nemo_get_queue_format(ctx, f->type);
	if (!pix_mp)
		return -EINVAL;

	f->fmt.pix_mp = *pix_mp;
	return 0;
}

static int nemo_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct nemo_ctx *ctx = container_of(priv, struct nemo_ctx, fh);
	struct v4l2_pix_format_mplane *pix_mp;
	int ret;

	pix_mp = nemo_get_queue_format(ctx, f->type);
	if (!pix_mp)
		return -EINVAL;

	ret = nemo_try_fmt(file, priv, f);
	if (ret)
		return ret;

	*pix_mp = f->fmt.pix_mp;
	return 0;
}

static int nemo_open(struct file *file)
{
	struct nemo_dev *dev = video_drvdata(file);
	struct nemo_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	nemo_fill_format(&ctx->out_fmt, 640, 480);
	nemo_fill_format(&ctx->cap_fmt, 640, 480);
	
	v4l2_fh_init(&ctx->fh, video_devdata(file));
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	return 0;
}

static int nemo_release(struct file *file)
{
	struct nemo_ctx *ctx = container_of(file->private_data, struct nemo_ctx, fh);

	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations nemo_fops = {
	.owner = THIS_MODULE,
	.open = nemo_open,
	.release = nemo_release,
	.unlocked_ioctl = video_ioctl2,
};

static int nemo_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "nemo", sizeof(cap->driver));
	strscpy(cap->card, "Nemo VPU Decoder", sizeof(cap->card));
	cap->device_caps = NEMO_CAPS;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static const struct v4l2_ioctl_ops nemo_ioctl_ops = {
	.vidioc_querycap = nemo_querycap,
	.vidioc_g_fmt_vid_out_mplane = nemo_g_fmt,
	.vidioc_g_fmt_vid_cap_mplane = nemo_g_fmt,
	.vidioc_s_fmt_vid_out_mplane = nemo_s_fmt,
	.vidioc_s_fmt_vid_cap_mplane = nemo_s_fmt,
	.vidioc_try_fmt_vid_out_mplane = nemo_try_fmt,
	.vidioc_try_fmt_vid_cap_mplane = nemo_try_fmt,
};

/*
 * The `compatible` string is the link between the Device Tree description and your driver.
 * When the kernel finds a platform device with that string, it can bind the device to this driver and call `nemo_probe()`. 
 * `MODULE_DEVICE_TABLE` makes the match available for module autoloading.
 */
static const struct of_device_id nemo_of_match[] = {
	{ .compatible = "ratspi,nemo-vpu" },
	{}
};
MODULE_DEVICE_TABLE(of, nemo_of_match);

static int __init nemo_init(void)
{
	struct nemo_dev *ndev;
	int ret;

	// Device APIs generally start with dev_/devm_
	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	strscpy(ndev->v4l2_dev.name, "nemo", sizeof(ndev->v4l2_dev.name));

	// platform_set_drvdata(pdev, ndev);

	ret = v4l2_device_register(NULL, &ndev->v4l2_dev);
	if (ret)
		goto err_free;

	strscpy(ndev->vid_dev.name, "nemo-vpu", sizeof(ndev->vid_dev.name));

	ndev->vid_dev.v4l2_dev = &ndev->v4l2_dev;
	ndev->vid_dev.fops = &nemo_fops;
	ndev->vid_dev.ioctl_ops = &nemo_ioctl_ops;
	ndev->vid_dev.release = video_device_release_empty;
	ndev->vid_dev.device_caps = NEMO_CAPS;

	video_set_drvdata(&ndev->vid_dev, ndev);

	ret = video_register_device(&ndev->vid_dev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_v4l2_unregister;

	g_nemo = ndev;
	pr_info("nemo: registered /dev/video%d\n", ndev->vid_dev.num);

	return 0;

err_v4l2_unregister:
	v4l2_device_unregister(&ndev->v4l2_dev);
err_free:
	kfree(ndev);
	return ret;
}

static void __exit nemo_exit(void)
{
	video_unregister_device(&g_nemo->vid_dev);
	v4l2_device_unregister(&g_nemo->v4l2_dev);
	kfree(g_nemo);
}

module_init(nemo_init);
module_exit(nemo_exit);

/* static struct platform_driver nemo_driver = {
	.probe = nemo_probe,
	.driver = {
		.name = "nemo-vpu",
		.of_match_table = nemo_of_match,
	},
};

module_platform_driver(nemo_driver); */

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nemo V4L2 decoder driver");
MODULE_AUTHOR("Rath");



