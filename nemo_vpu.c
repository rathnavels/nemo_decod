#include <linux/module.h>
#include <linux/platform_device.h>

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

static int nemo_probe(struct platform_device *pdev)
{
	struct nemo_dev *ndev;
	int ret;

	// All device APIs start with dev_/devm_
	ndev = devm_kzalloc(&pdev->dev, sizeof(*ndev), GFP_KERNEL);
	if (!ndev)
		return -ENOMEM;

	platform_set_drvdata(pdev, vpu);

	ret = v4l2_device_register(&pdev->dev, &ndev->v4l2_dev);
	if (ret)
		return ret;

	strscpy(ndev->vid_dev.name, "nemo-decod", sizeof(ndev->vid_dev.name));

	ndev->vid_dev.v4l2_dev = &ndev->v4l2_dev;

	return 0;
}



