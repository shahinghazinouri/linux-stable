/*
 * SoC Camera Driver for 24-bit RGB BT601 input.
 *
 * This driver does little other than tell the SoC bridge hardware what the
 * output format is.
 *
 * Copyright (C) 2012 Renesas Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>
#include <media/soc_camera.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-common.h>

#define MAX_WIDTH 1280
#define MAX_HEIGHT 800

struct rgb24bit_priv {
	struct v4l2_subdev	subdev;
	int			width;
	int			height;
};

static struct rgb24bit_priv *to_rgb24bit(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client), struct rgb24bit_priv,
			    subdev);
}

/* Get chip identification */
static int rgb24bit_g_chip_ident(struct v4l2_subdev *sd,
				struct v4l2_dbg_chip_ident *id)
{
	id->ident	= V4L2_IDENT_RGB24BIT;
	id->revision	= 1;

	return 0;
}

static int rgb24bit_g_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rgb24bit_priv *priv = to_rgb24bit(client);

	priv->width = MAX_WIDTH;
	priv->height =  MAX_HEIGHT;

	mf->width	= priv->width;
	mf->height	= priv->height;
	mf->code	= V4L2_MBUS_FMT_RGB888_1X24_LE;
	mf->colorspace	= V4L2_COLORSPACE_SRGB;
	mf->field	= V4L2_FIELD_NONE;

	return 0;
}

static int rgb24bit_s_fmt(struct v4l2_subdev *sd,
			struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rgb24bit_priv *priv = to_rgb24bit(client);

	priv->width = mf->width;
	priv->height = mf->height;

	return 0;
}

static int rgb24bit_try_fmt(struct v4l2_subdev *sd,
			  struct v4l2_mbus_framefmt *mf)
{
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = V4L2_MBUS_FMT_RGB888_1X24_LE;

	return 0;
}

static int rgb24bit_enum_fmt(struct v4l2_subdev *sd, unsigned int index,
			   enum v4l2_mbus_pixelcode *code)
{
	if (index > 0)
		return -EINVAL;

	*code = V4L2_MBUS_FMT_RGB888_1X24_LE;

	return 0;
}

static int rgb24bit_s_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct v4l2_rect *rect = &a->c;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rgb24bit_priv *priv = to_rgb24bit(client);

	priv->width = rect->width;
	priv->height = rect->height;

	rect->width = priv->width;
	rect->height = priv->height;
	rect->left = 0;
	rect->top = 0;

	return 0;
}

static int rgb24bit_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *a)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct rgb24bit_priv *priv = to_rgb24bit(client);

	if (priv) {
		a->c.width	= priv->width;
		a->c.height	= priv->height;
	} else {
		a->c.width	= MAX_WIDTH;
		a->c.height	= MAX_HEIGHT;
	}

	a->c.left	= 0;
	a->c.top	= 0;
	a->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE;

	return 0;
}

static int rgb24bit_cropcap(struct v4l2_subdev *sd, struct v4l2_cropcap *a)
{
	a->bounds.left			= 0;
	a->bounds.top			= 0;
	a->bounds.width			= MAX_WIDTH;
	a->bounds.height		= MAX_HEIGHT;
	a->defrect			= a->bounds;
	a->type				= V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a->pixelaspect.numerator	= 1;
	a->pixelaspect.denominator	= 1;

	return 0;
}

static int rgb24bit_video_probe(struct i2c_client *client)
{
	dev_info(&client->dev, "rgb24bit for 24-bit RGB input\n");

	return 0;
}

/* Request bus settings on camera side */
static int rgb24bit_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct soc_camera_link *icl = soc_camera_i2c_to_link(client);

	cfg->flags = V4L2_MBUS_PCLK_SAMPLE_RISING | V4L2_MBUS_MASTER |
		V4L2_MBUS_VSYNC_ACTIVE_LOW | V4L2_MBUS_HSYNC_ACTIVE_LOW |
		V4L2_MBUS_DATA_ACTIVE_HIGH;
	cfg->type = V4L2_MBUS_PARALLEL;
	cfg->flags = soc_camera_apply_board_flags(icl, cfg);

	return 0;
}

static struct v4l2_subdev_core_ops rgb24bit_core_ops = {
	.g_chip_ident		= rgb24bit_g_chip_ident,
};

static struct v4l2_subdev_video_ops rgb24bit_video_ops = {
	.g_mbus_fmt	= rgb24bit_g_fmt,
	.s_mbus_fmt	= rgb24bit_s_fmt,
	.try_mbus_fmt	= rgb24bit_try_fmt,
	.enum_mbus_fmt	= rgb24bit_enum_fmt,
	.cropcap	= rgb24bit_cropcap,
	.g_crop		= rgb24bit_g_crop,
	.s_crop		= rgb24bit_s_crop,
	.g_mbus_config	= rgb24bit_g_mbus_config,
};

static struct v4l2_subdev_ops rgb24bit_subdev_ops = {
	.core	= &rgb24bit_core_ops,
	.video	= &rgb24bit_video_ops,
};

/*
 * i2c_driver function
 */
static __devinit int rgb24bit_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct rgb24bit_priv *priv;
	struct soc_camera_link *icl = soc_camera_i2c_to_link(client);
	int ret;

	if (!icl) {
		dev_err(&client->dev, "Missing platform_data for driver\n");
		return -EINVAL;
	}

	priv = kzalloc(sizeof(struct rgb24bit_priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&client->dev,
			"Failed to allocate memory for private data!\n");
		return -ENOMEM;
	}

	v4l2_i2c_subdev_init(&priv->subdev, client, &rgb24bit_subdev_ops);

	ret = rgb24bit_video_probe(client);
	if (ret)
		kfree(priv);

	return ret;
}

static __devexit int rgb24bit_remove(struct i2c_client *client)
{
	struct rgb24bit_priv *priv = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(&priv->subdev);
	kfree(priv);
	return 0;
}

static const struct i2c_device_id rgb24bit_id[] = {
	{ "rgb24bit", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rgb24bit_id);

static struct i2c_driver rgb24bit_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "rgb24bit",
	},
	.probe    = rgb24bit_probe,
	.remove   = rgb24bit_remove,
	.id_table = rgb24bit_id,
};

module_i2c_driver(rgb24bit_i2c_driver);

MODULE_DESCRIPTION("SoC Camera driver for 24-bit RGB input");
MODULE_AUTHOR("Phil Edworthy <phil.edworthy@renesas.com>");
MODULE_LICENSE("GPL v2");
