#include <linux/module.h>
#include <linux/slab.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>	// v4l2 framework level device
#include <media/v4l2-dev.h>	// video_device that becomes /dev/videoX

/* 
 * A platform device is something that is not enumerated automatically.
 * It generally is defined in the device tree source.
 * A platform driver is the software knob of the device.
 * Platform driver struct has the definition for probe, remove etc.
 * 
 * Apparently drivers can be platform drivers which define a platform device with probe etc,
 * or simple software drivers with module init and exit.
 * 
 * Only platform device drivers have probe.  Software driver is built with init and exit.
 */

struct nemo_dev {
	struct v4l2_device v4l2_dev;	// v4l2 device 
	struct video_device vid_dev;	// video device -> /dev/videoX
};

static struct nemo_dev *g_nemo;

static const struct v4l2_file_operations nemo_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = video_ioctl2,
};

static int nemo_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "nemo", sizeof(cap->driver));
	strscpy(cap->card, "Nemo VPU Decoder", sizeof(cap->card));
	cap->device_caps = V4L2_CAP_VIDEO_M2M;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	return 0;
}

static const struct v4l2_ioctl_ops nemo_ioctl_ops = {
	.vidioc_querycap = nemo_querycap,
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
	ndev->vid_dev.device_caps = V4L2_CAP_VIDEO_M2M;

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



