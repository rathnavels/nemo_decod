#include <linux/module.h>
#include <linux/platform_device.h>

#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>	// v4l2 framework level device
#include <media/v4l2-dev.h>	// video_device that becomes /dev/videoX

/* 
 * A platform device is something that is not enumerated automatically.
 * It must be defined in the device tree source.
 * A platform driver is the software knob of the device.
 * Platform driver struct has the definition for probe, remove etc.
 */

struct nemo_dev {
	struct v4l2_device v4l2_dev;	// v4l2 device 
	struct video_device vid_dev;	// video device -> /dev/videoX
};

static const struct v4l2_file_operations nemo_fops = {
	.owner = THIS_MODULE,
};

static int nemo_querycap(struct file *file, void *priv, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "nemo", sizeof(cap->driver));
	strscpy(cap->card, "Nemo VPU Decoder", sizeof(cap->card));

	return 0;
}

static const struct v4l2_ioctl_ops nemo_ioctl_ops = {
	.vidioc_querycap = nemo_querycap,
};

static int nemo_probe(struct platform_device *pdev)
{
	struct nemo_dev *ndev;
	int ret;

	// Device APIs generally start with dev_/devm_
	ndev = devm_kzalloc(&pdev->dev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, ndev);

	ret = v4l2_device_register(&pdev->dev, &ndev->v4l2_dev);
	if (ret)
		return ret;

	strscpy(ndev->vid_dev.name, "nemo-decod", sizeof(ndev->vid_dev.name));

	ndev->vid_dev.v4l2_dev = &ndev->v4l2_dev;
	ndev->vid_dev.fops = &nemo_fops;
	ndev->vid_dev.ioctl_ops = &nemo_ioctl_ops;

	return 0;
}

static struct platform_driver nemo_driver = {
	.probe = nemo_probe,
	.driver = {
		.name = "nemo-vpu",
	},
};

module_platform_driver(nemo_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Nemo V4L2 decoder driver");
MODULE_AUTHOR("Rath");



