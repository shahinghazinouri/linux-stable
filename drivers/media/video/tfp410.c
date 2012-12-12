/*
 * Very simple driver used to power on a TFP410
 *
 * Copyright(c) 2011 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/i2c.h>

#define TFP410_CTL_1_MODE 0x8
#define RSVD (1 << 7)
#define TDIS (1 << 6)
#define VEN  (1 << 5)
#define HEN  (1 << 4)
#define DSEL (1 << 3)
#define BSEL (1 << 2)
#define EDGE (1 << 1)
#define PD   (1 << 0)
#define TFP410_CTL_1_ON   (VEN | HEN | DSEL | BSEL | EDGE | PD)

static int tfp410_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	union i2c_smbus_data data;
	data.byte = TFP410_CTL_1_ON;

	return i2c_smbus_xfer(i2c->adapter, i2c->addr, 0, I2C_SMBUS_WRITE,
		TFP410_CTL_1_MODE, I2C_SMBUS_BYTE_DATA, &data);
}

static const struct i2c_device_id tfp410_i2c_id[] = {
	{ "tfp410", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tfp410_i2c_id);

static struct i2c_driver tfp410_i2c_driver = {
	.driver = {
		.name = "TFP410 I2C Driver",
		.owner = THIS_MODULE,
	},
	.probe		= tfp410_i2c_probe,
	.id_table	= tfp410_i2c_id,
};

static void __exit tfp410_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&tfp410_i2c_driver);
#endif
}

static int __init tfp410_init(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&tfp410_i2c_driver);
#endif
	return ret;
}

module_init(tfp410_init);
module_exit(tfp410_exit);

MODULE_DESCRIPTION("TFP410 I2C Driver");
MODULE_AUTHOR("Stephen Lawrence <stephen.lawrence@renesas.com>");
MODULE_LICENSE("GPL");
