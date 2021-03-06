/*
 * hurricane board support
 *
 * Copyright (C) 2012  Renesas Electronics Europe
 * Copyright (C) 2012  James Gomez
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/spi/sh_hspi.h>
#include <linux/sh_eth.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/usb/rcar-usb.h>
#include <mach/hpb-dmae.h>
#include <video/rcarfb.h>
#include <media/rcarvin.h>
#include <mach/hardware.h>
#include <mach/r8a7779.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

static struct i2c_board_info hurricane_i2c_devices[] = {
	{ I2C_BOARD_INFO("ak4642", 0x12), },
	{ I2C_BOARD_INFO("ak4642", 0x13), },
	{ I2C_BOARD_INFO("tfp410", 0x3f), },
#ifdef CONFIG_MACH_HURRICANE_DU1
	{ I2C_BOARD_INFO("tfp410", 0x3e), },
#endif
};

/* Fixed 3.3V regulator to be used by SDHI0 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* SMSC LAN89211 */
static struct resource smsc911x_resources[] = {
	[0] = {
	#ifdef CONFIG_MACH_HURRICANE_EMMC
		.start		= 0x04000000, /* CS1 */
		.end		= 0x040000ff, /* A1->A7 */
	#else
		.start		= 0x18000000, /* ExCS0 */
		.end		= 0x180000ff, /* A1->A7 */
	#endif
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(30), /* IRQ 3 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= -1,
	.dev  = {
		.platform_data = &smsc911x_platdata,
	},
	.resource	= smsc911x_resources,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
};

static struct resource rcar_eth_resources[] = {
	[0] = {
		.name   = "sh-eth",
		.start = 0xfde00200,
		.end   = 0xfde003fc,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gic_spi(148),
		.flags = IORESOURCE_IRQ,
	},
};

static struct sh_eth_plat_data rcar_eth_plat = {
	.phy = 0x1f, /* SMSC LAN8700 */
	.edmac_endian = EDMAC_LITTLE_ENDIAN,
	.register_type = SH_ETH_REG_FAST_SH4,
	.phy_interface = PHY_INTERFACE_MODE_RMII,
	.ether_link_active_low = 1
};

static struct platform_device rcar_eth_device = {
	.name = "sh-eth",
	.id	= 0,
	.dev = {
		.platform_data = &rcar_eth_plat,
	},
	.num_resources = ARRAY_SIZE(rcar_eth_resources),
	.resource = rcar_eth_resources,
};

/* This function parses Renesas on-chip ethernet command line args,
   e.g. rmac=14:fe:b6:a5:33:21 */
static void __init parse_mac_addr(char *macstr)
{
	int i, j;
	unsigned char result, value;

	for (i = 0; i < 6; i++) {
		result = 0;

		if (i != 5 && *(macstr + 2) != ':')
			return;

		for (j = 0; j < 2; j++) {
			if (isxdigit(*macstr)
			    && (value = isdigit(*macstr) ? *macstr - '0'
				      : toupper(*macstr) - 'A' + 10) < 16) {
				result = result * 16 + value;
				macstr++;
			} else
				return;
		}

		macstr++;
		rcar_eth_plat.mac_addr[i] = result;
	}
}


static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "sdhi0",
		.start	= 0xffe4c000,
		.end	= 0xffe4c0ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(104),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mobile_sdhi_info sdhi0_platform_data = {
	.dma_slave_tx	= HPBDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= HPBDMA_SLAVE_SDHI0_RX,
	.tmio_flags = TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps = MMC_CAP_SD_HIGHSPEED,
};

static struct platform_device sdhi0_device = {
	.name = "sh_mobile_sdhi",
	.num_resources = ARRAY_SIZE(sdhi0_resources),
	.resource = sdhi0_resources,
	.id = 0,
	.dev = {
		.platform_data = &sdhi0_platform_data,
	}
};

#ifdef CONFIG_MACH_HURRICANE_EMMC
/* SH_MMCIF */
static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "MMCIF",
		.start	= 0xFFE5A000,
		.end	= 0xFFE5A07F,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* MMC ERR */
		.start	= IRQ_MMC_0_ERR,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		/* MMC ACC */
		.start	= IRQ_MMC_0_ACC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_plat = {
	.sup_pclk	= 0,
	.ocr		= MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps		= MMC_CAP_4_BIT_DATA |
			  MMC_CAP_8_BIT_DATA |
			  MMC_CAP_NONREMOVABLE,	  /* eMMC so non removable */
	.slave_id_tx	= HPBDMA_SLAVE_MMC0_TX,
	.slave_id_rx	= HPBDMA_SLAVE_MMC0_RX,
};

static struct platform_device sh_mmcif_device = {
	.name		= "sh_mmcif",
	.id		= 0,
	.dev		= {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh_mmcif_plat,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};
#endif


/* Thermal */
static struct resource thermal_resources[] = {
	[0] = {
		.start		= 0xFFC48000,
		.end		= 0xFFC48038 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device thermal_device = {
	.name		= "rcar_thermal",
	.resource	= thermal_resources,
	.num_resources	= ARRAY_SIZE(thermal_resources),
};

/* HSPI */
static struct resource hspi_resources[] = {
	[0] = {
		.start		= 0xFFFC7000,
		.end		= 0xFFFC7018 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device hspi_device = {
	.name	= "sh-hspi",
	.id	= 0,
	.resource	= hspi_resources,
	.num_resources	= ARRAY_SIZE(hspi_resources),
};

static struct spi_board_info marzen_spi_devices[] __initdata = {
	{
		.modalias = "spidev",
		.max_speed_hz = 100000000,
		.bus_num = 0,
		.chip_select = 0,
	},
};

static struct resource rcar_du0_resources[] = {
	[0] = {
		.name	= "Display Unit 0",
		.start	= 0xfff80000,
		.end	= 0xfffb1007,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(31),
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct fb_videomode extra_video_modes[] = {
	/* 640x480-60 VESA */
	{ NULL, 60, 640, 480, 39682,  48, 16, 33, 10, 96, 2, 0, 0 },
	/* 800x600-56 VESA */
	{ NULL, 56, 800, 600, 27777, 128, 24, 22, 01, 72, 2, 0, 0 },
	/* 1024x768-60 VESA */
	{ NULL, 60, 1024, 768, 15384, 160, 24, 29, 3, 136, 6, 0, 0 },
	/* 800x480 */
	{ NULL, 75, 800, 480, 23922, 128, 24, 33, 10, 96, 2, 0, 0 },
	/* 1024x600 */
	{ NULL, 60, 1024, 600, 11764, 120, 25, 100, 65, 220, 36, 0, 0 },
	/* 1280x768 */
	{ NULL, 50, 1280, 768, 11764, 97, 131, 59, 63, 122, 21, 0, 0 },
	/* 1280x720p @ 50Hz */
	{ NULL, 50, 1280, 720, 13468, 220, 440, 20, 5, 40, 5, 0, 0 },
};

static struct rcar_reso_info rcar_reso_par_0 = {
	.num_modes = ARRAY_SIZE(extra_video_modes),
	.modes = extra_video_modes,
};

static struct platform_device rcar_display_device = {
	.name		= "rcarfb",
	.num_resources	= ARRAY_SIZE(rcar_du0_resources),
	.resource		= rcar_du0_resources,
	.dev	= {
		.platform_data = &rcar_reso_par_0,
		.coherent_dma_mask = ~0,
	},
};

#ifdef CONFIG_MACH_HURRICANE_DU1
static struct resource rcar_du1_resources[] = {
	[0] = {
		.name	= "Display Unit 1",
		.start	= 0xfff80000,
		.end	= 0xfffb1007,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(31),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rcar_reso_info rcar_reso_par_1 = {
	.num_modes = ARRAY_SIZE(extra_video_modes),
	.modes = extra_video_modes,
};

static struct platform_device rcar_display1_device = {
	.id		= 1,
	.name		= "rcarfb",
	.num_resources	= ARRAY_SIZE(rcar_du1_resources),
	.resource	= rcar_du1_resources,
	.dev	= {
		.platform_data = &rcar_reso_par_1,
		.coherent_dma_mask = ~0,
	},
};
#endif

static u64 usb_dmamask = ~(u32)0;

static struct resource ehci0_resources[] = {
	[0] = {
		.start	= 0xffe70000,
		.end	= 0xffe70000 + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBH_0_EHCI,
		.flags	= IORESOURCE_IRQ,
	},
};


static struct rcar_usb_info ehci0_info = {
	.port_offset = 0,
};

struct platform_device ehci0_device = {
	.name	= "rcar_ehci",
	.id	= 0,
	.dev	= {
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehci0_info,
	},
	.num_resources	= ARRAY_SIZE(ehci0_resources),
	.resource	= ehci0_resources,
};

static struct resource ohci0_resources[] = {
	[0] = {
		.start	= 0xffe70400,
		.end	= 0xffe70400 + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBH_0_OHCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rcar_usb_info ohci0_info = {
	.port_offset = 0,
};

struct platform_device ohci0_device = {
	.name	= "rcar_ohci",
	.id	= 0,
	.dev	= {
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohci0_info,
	},
	.num_resources	= ARRAY_SIZE(ohci0_resources),
	.resource	= ohci0_resources,
};

static struct resource ehci1_resources[] = {
	[0] = {
		.start	= 0xfff70000,
		.end	= 0xfff70000 + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBH_1_EHCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rcar_usb_info ehci1_info = {
	.port_offset = 1,
};

struct platform_device ehci1_device = {
	.name	= "rcar_ehci",
	.id	= 1,
	.dev	= {
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ehci1_info,
	},
	.num_resources	= ARRAY_SIZE(ehci1_resources),
	.resource	= ehci1_resources,
};

static struct resource ohci1_resources[] = {
	[0] = {
		.start	= 0xfff70400,
		.end	= 0xfff70400 + 0x3ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_USBH_1_OHCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rcar_usb_info ohci1_info = {
	.port_offset = 1,
};

struct platform_device ohci1_device = {
	.name	= "rcar_ohci",
	.id	= 1,
	.dev	= {
		.dma_mask		= &usb_dmamask,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &ohci1_info,
	},
	.num_resources	= ARRAY_SIZE(ohci1_resources),
	.resource	= ohci1_resources,
};

#define RC_USBH0_BASE 0xffe70000
#define RC_USBH1_BASE 0xfff70000

/* USBH common register */
#define USBPCTRL0		0x0800
#define USBPCTRL1		0x0804
#define USBST			0x0808
#define USBEH0			0x080C
#define USBOH0			0x081C
#define USBCTL0			0x0858
#define EIIBC1			0x0094
#define EIIBC2			0x009C

/* bit field of Port Control 0 register */
#define	USBPCTRL0_PORT1		0x00000001
#define	USBPCTRL0_OVC1		0x00000002
#define	USBPCTRL0_OVC0		0x00000008
#define	USBPCTRL0_PENC		0x00000010
#define	USBPCTRL0_PORT0		0x00000100
#define	USBPCTRL0_OVC1_VBUS1	0x00000200

/* bit field of Port Control 1 register */
#define	USBPCTRL1_PHYENB	0x00000001
#define	USBPCTRL1_PLLENB	0x00000002
#define	USBPCTRL1_PHYRST	0x00000004
#define	USBPCTRL1_RST		0x80000000

/* bit field of USB Status register */
#define	USBST_PLL		0x40000000
#define	USBST_ACT		0x80000000

/* bit field of USB Control0 register */
#define	USBCTL0_CLKSEL		0x00000080

/* bit field of EHCI IP Buffer Control register 2 */
#define	EIIBC2_BUF_EN		0x00000001

/* other */
#define EHCI_IP_BUF		0x00FF0040
#define SWAP_NONE		0x00000000
#define SWAP_BYTE		0x00000001
#define SWAP_WORD		0x00000002
#define SWAP_WORD_BYTE		0x00000003

static int __init rcar_usbh_start(void)
{
	u32 val;
	int timeout;
	struct clk *clk, *clk2;

	void __iomem *usb_base = ioremap_nocache(RC_USBH0_BASE, 0x900);
	void __iomem *usb_base1 = ioremap_nocache(RC_USBH1_BASE, 0x900);

	/* enable clocks USB0/1 and 2 */
	clk = clk_get(NULL, "usb_fck");
	if (IS_ERR(clk)) {
		printk(KERN_ERR "Can't get usb clock\n");
		return -1;
	}
	clk_enable(clk);

	clk2 = clk_get(NULL, "usb_fck2");
	if (IS_ERR(clk2)) {
		printk(KERN_ERR "Can't get usb clock 2\n");
		return -1;
	}
	clk_enable(clk2);

	/*
	 * Port Control 1 Setting
	 */
	/* clear stand-by */
	iowrite32(USBPCTRL1_PHYENB, (usb_base + USBPCTRL1));

	/* start PLL */
	iowrite32((USBPCTRL1_PLLENB | USBPCTRL1_PHYENB),
		(usb_base + USBPCTRL1));

	/* work around of USB-PHY */
#ifdef CONFIG_USB_PHY_MARZEN_010S
	iowrite32(0x10700040, (usb_base + 0x0850));
	iowrite32(0x00007700, (usb_base + 0x085C));
#elif defined CONFIG_USB_PHY_MARZEN_110S
	iowrite32(0x10B00040, (usb_base + 0x0850));
	iowrite32(0x00007700, (usb_base + 0x085C));
#endif

	/* check status */
	timeout = 100; /* about 100ms */
	do {
		msleep(1);
		val = ioread32(usb_base + USBST);
		if ((val & USBST_ACT) && (val & USBST_PLL))
			break;
	} while (--timeout > 0);
	if (timeout == 0) {
		printk(KERN_ERR "USB is not ready. [%08x]\n", val);
		return -1;
	}

	/* clear reset */
	iowrite32((USBPCTRL1_PHYRST | USBPCTRL1_PLLENB | USBPCTRL1_PHYENB),
		(usb_base + USBPCTRL1));

	/*
	 * Port Control 0 Setting
	 */
	iowrite32(0, (usb_base + USBPCTRL0));

	/*
	 * EHCI IP Internal Buffer Setting
	 */
	iowrite32(EHCI_IP_BUF, (usb_base + EIIBC1));
	iowrite32(EIIBC2_BUF_EN, (usb_base + EIIBC2));

	iowrite32(EHCI_IP_BUF, (usb_base1 + EIIBC1));
	iowrite32(EIIBC2_BUF_EN, (usb_base1 + EIIBC2));

	/*
	 * Bus Alignment Setting
	 */
	iowrite32(SWAP_NONE, (usb_base + USBEH0));
	iowrite32(SWAP_NONE, (usb_base + USBOH0));

	iounmap(usb_base);
	iounmap(usb_base1);

	return 0;
}

static int __init rcar_usbh_init(void)
{
	if (rcar_usbh_start()) {
		platform_device_unregister(&ehci0_device);
		platform_device_unregister(&ohci0_device);
		platform_device_unregister(&ehci1_device);
		platform_device_unregister(&ohci1_device);
	}

	return 0;
}

static struct resource rcar_sata_resources[] = {
	[0] = {
		.name	= "sata",
		.start	= 0xfc600000,
		.end	= 0xfc601fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(100),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device rcar_sata_device = {
	.name		= "sata_rcar",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(rcar_sata_resources),
	.resource	= rcar_sata_resources,
	.dev  = {
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device alsa_soc_platform_device = {
	.name		= "rcar_alsa_soc_platform",
	.id		= 0,
};

static struct resource sru_resources[] = {
	[0] = {
		.name   = "sru",
		.start  = 0xffd90000,
		.end    = 0xffd97fff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.name   = "adg",
		.start  = 0xfffe0000,
		.end    = 0xfffe1000,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device sru_device = {
	.name		= "rcar_sru",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sru_resources),
	.resource	= sru_resources,
};

static struct resource rcar_vin0_resources[] = {
	[0] = {
		.name = "VIN0",
		.start = 0xffc50000,
		.end = 0xffc50fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gic_spi(63),
		.flags = IORESOURCE_IRQ,
	},
};
static struct resource rcar_vin1_resources[] = {
	[0] = {
		.name = "VIN1",
		.start = 0xffc51000,
		.end = 0xffc51fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gic_spi(64),
		.flags = IORESOURCE_IRQ,
	},
};
static struct resource rcar_vin2_resources[] = {
	[0] = {
		.name = "VIN2",
		.start = 0xffc52000,
		.end = 0xffc52fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gic_spi(65),
		.flags = IORESOURCE_IRQ,
	},
};
static struct resource rcar_vin3_resources[] = {
	[0] = {
		.name = "VIN3",
		.start = 0xffc53000,
		.end = 0xffc53fff,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = gic_spi(66),
		.flags = IORESOURCE_IRQ,
	},
};

static struct rcar_vin_info rcar_vin_info = {};
static struct rcar_vin_info rcar_vin_10bit_info = {
	.format = V4L2_MBUS_FMT_YUYV10_2X10,
};
static u64 rcarvin_dmamask = DMA_BIT_MASK(32);

static struct platform_device rcar_vin0_device = {
	.name  = "rcar_vin",
	.id = 0,
	.num_resources = ARRAY_SIZE(rcar_vin0_resources),
	.resource  = rcar_vin0_resources,
	.dev  = {
		.dma_mask = &rcarvin_dmamask,
#ifdef CONFIG_MACH_HURRICANE_10BIT_CAMERAS
		.platform_data = &rcar_vin_10bit_info,
#else
		.platform_data = &rcar_vin_info,
#endif
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device rcar_vin1_device = {
	.name  = "rcar_vin",
	.id = 1,
	.num_resources = ARRAY_SIZE(rcar_vin1_resources),
	.resource  = rcar_vin1_resources,
	.dev  = {
		.dma_mask = &rcarvin_dmamask,
#ifdef CONFIG_MACH_HURRICANE_10BIT_CAMERAS
		.platform_data = &rcar_vin_10bit_info,
#else
		.platform_data = &rcar_vin_info,
#endif
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device rcar_vin2_device = {
	.name  = "rcar_vin",
	.id = 2,
	.num_resources = ARRAY_SIZE(rcar_vin2_resources),
	.resource  = rcar_vin2_resources,
	.dev  = {
		.dma_mask = &rcarvin_dmamask,
		.platform_data = &rcar_vin_info,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device rcar_vin3_device = {
	.name  = "rcar_vin",
	.id = 3,
	.num_resources = ARRAY_SIZE(rcar_vin3_resources),
	.resource  = rcar_vin3_resources,
	.dev  = {
		.dma_mask = &rcarvin_dmamask,
		.platform_data = &rcar_vin_info,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct i2c_board_info hurricane_i2c_camera[] = {
	{ I2C_BOARD_INFO("ov10635", 0x30), },
	{ I2C_BOARD_INFO("ov10635", 0x31), },
	{ I2C_BOARD_INFO("ov10635", 0x32), },
	{ I2C_BOARD_INFO("ov10635", 0x34), },
};

static void camera_power_on(void)
{
	return;
}

static void camera_power_off(void)
{
	return;
}

static int ov10635_power(struct device *dev, int mode)
{
	if (mode)
		camera_power_on();
	else
		camera_power_off();

	return 0;
}

static struct soc_camera_link ov10635_ch0_link = {
	.bus_id = 0,
	.power  = ov10635_power,
	.board_info = &hurricane_i2c_camera[0],
	.i2c_adapter_id = 2,
	.module_name = "ov10635",
};

static struct soc_camera_link ov10635_ch1_link = {
	.bus_id = 1,
	.power  = ov10635_power,
	.board_info = &hurricane_i2c_camera[1],
	.i2c_adapter_id = 2,
	.module_name = "ov10635",
};

static struct soc_camera_link ov10635_ch2_link = {
	.bus_id = 2,
	.power  = ov10635_power,
	.board_info = &hurricane_i2c_camera[2],
	.i2c_adapter_id = 2,
	.module_name = "ov10635",
};

static struct soc_camera_link ov10635_ch3_link = {
	.bus_id = 3,
	.power  = ov10635_power,
	.board_info = &hurricane_i2c_camera[3],
	.i2c_adapter_id = 2,
	.module_name = "ov10635",
};

static struct platform_device rcar_camera[] = {
	{
		.name = "soc-camera-pdrv",
		.id = 0,
		.dev = {
			.platform_data = &ov10635_ch0_link,
		},
	},
	{
		.name = "soc-camera-pdrv",
		.id = 1,
		.dev = {
			.platform_data = &ov10635_ch1_link,
		},
	},
	{
		.name = "soc-camera-pdrv",
		.id = 2,
		.dev = {
			.platform_data = &ov10635_ch2_link,
		},
	},
	{
		.name = "soc-camera-pdrv",
		.id = 3,
		.dev = {
			.platform_data = &ov10635_ch3_link,
		},
	},
};

static struct platform_device *hurricane_devices[] __initdata = {
	&eth_device,
	&sdhi0_device,
#ifdef CONFIG_MACH_HURRICANE_EMMC
	&sh_mmcif_device,
#endif
	&thermal_device,
	&hspi_device,
	&ehci0_device,
	&ohci0_device,
	&ehci1_device,
	&ohci1_device,
	&rcar_sata_device,
	&alsa_soc_platform_device,
	&sru_device,
	&rcar_display_device,
	&rcar_eth_device,
	&rcar_vin0_device,
	&rcar_vin1_device,
#ifdef CONFIG_MACH_HURRICANE_DU1
	&rcar_display1_device,
#elif defined CONFIG_MACH_HURRICANE_VIN2
	&rcar_vin2_device,
#endif
	&rcar_vin3_device,
	&rcar_camera[0],
	&rcar_camera[1],
#ifdef CONFIG_MACH_HURRICANE_VIN2
	&rcar_camera[2],
#endif
	&rcar_camera[3],
};

static void __init hurricane_init(void)
{
	regulator_register_fixed(0, fixed3v3_power_consumers,
				ARRAY_SIZE(fixed3v3_power_consumers));
	regulator_register_fixed(0, dummy_supplies,
				ARRAY_SIZE(dummy_supplies));

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, 64K*16way */
	l2x0_init(IOMEM(0xf0100000), 0x40070000, 0x82000fff);
#endif

	r8a7779_pinmux_init();

	/* SCIF1 */
	gpio_request(GPIO_FN_TX1, NULL);
	gpio_request(GPIO_FN_RX1, NULL);

	/* SCIF4 */
	gpio_request(GPIO_FN_TX4, NULL);
	gpio_request(GPIO_FN_RX4, NULL);

	/* SCIF3_B */
	gpio_request(GPIO_FN_TX3_B_IRDA_TX_B, NULL);
	gpio_request(GPIO_FN_RX3_B_IRDA_RX_B, NULL);

	/* LAN89211 */
#ifdef CONFIG_MACH_HURRICANE_EMMC
	gpio_request(GPIO_FN_CS1_A26, NULL); /* nCS */
#else
	gpio_request(GPIO_FN_EX_CS0, NULL); /* nCS */
#endif
	gpio_request(GPIO_FN_IRQ3, NULL); /* IRQ */

#ifdef CONFIG_MACH_HURRICANE_EMMC
	/* MMCIF */
	gpio_request(GPIO_FN_MMC0_D0, NULL);
	gpio_request(GPIO_FN_MMC0_D1, NULL);
	gpio_request(GPIO_FN_MMC0_D2, NULL);
	gpio_request(GPIO_FN_MMC0_D3, NULL);
	gpio_request(GPIO_FN_MMC0_D4, NULL);
	gpio_request(GPIO_FN_MMC0_D5, NULL);
	gpio_request(GPIO_FN_MMC0_D6, NULL);
	gpio_request(GPIO_FN_MMC0_D7, NULL);
	gpio_request(GPIO_FN_MMC0_CMD, NULL);
	gpio_request(GPIO_FN_MMC0_CLK, NULL);
#endif

	/* SD0 (CN20) */
	gpio_request(GPIO_FN_SD0_CLK, NULL);
	gpio_request(GPIO_FN_SD0_CMD, NULL);
	gpio_request(GPIO_FN_SD0_DAT0, NULL);
	gpio_request(GPIO_FN_SD0_DAT1, NULL);
	gpio_request(GPIO_FN_SD0_DAT2, NULL);
	gpio_request(GPIO_FN_SD0_DAT3, NULL);
	gpio_request(GPIO_FN_SD0_CD, NULL);
	gpio_request(GPIO_FN_SD0_WP, NULL);

	/* SD control registers IOCTRLn: SD pins driving ability */
	{
		void __iomem *base = ioremap_nocache(0xfffc0000, 0x100);
		iowrite32(~0x9aaa9aaa, base);		/* PMMR */
		iowrite32(0x9aaa9aaa, base + 0x60);	/* IOCTRL0 */
		iowrite32(~0x80009aaa, base);		/* PMMR */
		iowrite32(0x80009aaa, base + 0x64);	/* IOCTRL1 */
		iowrite32(~0x80009aaa, base);		/* PMMR */
		iowrite32(0x80009aaa, base + 0x68);	/* IOCTRL2 */
		iowrite32(~0x000001aa, base);		/* PMMR */
		iowrite32(0x000001aa, base + 0x6c);	/* IOCTRL3 */
		iounmap(base);
	}

	/* HSPI 0 */
	gpio_request(GPIO_FN_HSPI_CLK0,	NULL);
	gpio_request(GPIO_FN_HSPI_CS0,	NULL);
	gpio_request(GPIO_FN_HSPI_TX0,	NULL);
	gpio_request(GPIO_FN_HSPI_RX0,	NULL);

	/* USB (CN22) */
	gpio_request(GPIO_FN_USB_PENC2, NULL);

	/* Display Unit 0 (CN10: ARGB0) */
	gpio_request(GPIO_FN_DU0_DR7, NULL);
	gpio_request(GPIO_FN_DU0_DR6, NULL);
	gpio_request(GPIO_FN_DU0_DR5, NULL);
	gpio_request(GPIO_FN_DU0_DR4, NULL);
	gpio_request(GPIO_FN_DU0_DR3, NULL);
	gpio_request(GPIO_FN_DU0_DR2, NULL);
	gpio_request(GPIO_FN_DU0_DR1, NULL);
	gpio_request(GPIO_FN_DU0_DR0, NULL);
	gpio_request(GPIO_FN_DU0_DG7, NULL);
	gpio_request(GPIO_FN_DU0_DG6, NULL);
	gpio_request(GPIO_FN_DU0_DG5, NULL);
	gpio_request(GPIO_FN_DU0_DG4, NULL);
	gpio_request(GPIO_FN_DU0_DG3, NULL);
	gpio_request(GPIO_FN_DU0_DG2, NULL);
	gpio_request(GPIO_FN_DU0_DG1, NULL);
	gpio_request(GPIO_FN_DU0_DG0, NULL);
	gpio_request(GPIO_FN_DU0_DB7, NULL);
	gpio_request(GPIO_FN_DU0_DB6, NULL);
	gpio_request(GPIO_FN_DU0_DB5, NULL);
	gpio_request(GPIO_FN_DU0_DB4, NULL);
	gpio_request(GPIO_FN_DU0_DB3, NULL);
	gpio_request(GPIO_FN_DU0_DB2, NULL);
	gpio_request(GPIO_FN_DU0_DB1, NULL);
	gpio_request(GPIO_FN_DU0_DB0, NULL);
	gpio_request(GPIO_FN_DU0_EXVSYNC_DU0_VSYNC, NULL);
	gpio_request(GPIO_FN_DU0_EXHSYNC_DU0_HSYNC, NULL);
	gpio_request(GPIO_FN_DU0_DOTCLKOUT0, NULL);
	gpio_request(GPIO_FN_DU0_DOTCLKOUT1, NULL);
	gpio_request(GPIO_FN_DU0_DISP, NULL);

	/* I2C 2_C */
	gpio_request(GPIO_FN_SCL2_C, NULL);
	gpio_request(GPIO_FN_SDA2_C, NULL);

	/* SSI */
	gpio_request(GPIO_FN_SSI_SDATA0, NULL);
	gpio_request(GPIO_FN_SSI_SDATA1, NULL);
	gpio_request(GPIO_FN_SSI_SCK0129, NULL);
	gpio_request(GPIO_FN_SSI_WS0129, NULL);

	gpio_request(GPIO_FN_SSI_SDATA7, NULL);
	gpio_request(GPIO_FN_SSI_SDATA8, NULL);
	gpio_request(GPIO_FN_SSI_SCK78, NULL);
	gpio_request(GPIO_FN_SSI_WS78, NULL);

	/* Audio Clock */
	gpio_request(GPIO_FN_AUDIO_CLKA, NULL);
	gpio_request(GPIO_FN_AUDIO_CLKB, NULL);

	/* VIN0 */
	gpio_request(GPIO_FN_VI0_CLK, NULL);
#ifdef CONFIG_MACH_HURRICANE_10BIT_CAMERAS
	gpio_request(GPIO_FN_VI0_G1, NULL);
	gpio_request(GPIO_FN_VI0_G0, NULL);
#endif
	gpio_request(GPIO_FN_VI0_DATA7_VI0_B7, NULL);
	gpio_request(GPIO_FN_VI0_DATA6_VI0_B6, NULL);
	gpio_request(GPIO_FN_VI0_DATA5_VI0_B5, NULL);
	gpio_request(GPIO_FN_VI0_DATA4_VI0_B4, NULL);
	gpio_request(GPIO_FN_VI0_DATA3_VI0_B3, NULL);
	gpio_request(GPIO_FN_VI0_DATA2_VI0_B2, NULL);
	gpio_request(GPIO_FN_VI0_DATA1_VI0_B1, NULL);
	gpio_request(GPIO_FN_VI0_DATA0_VI0_B0, NULL);

	/* VIN1 */
	gpio_request(GPIO_FN_VI1_CLK, NULL);
#ifdef CONFIG_MACH_HURRICANE_10BIT_CAMERAS
	gpio_request(GPIO_FN_VI1_G1, NULL);
	gpio_request(GPIO_FN_VI1_G0, NULL);
#endif
	gpio_request(GPIO_FN_VI1_DATA7_VI1_B7, NULL);
	gpio_request(GPIO_FN_VI1_DATA6_VI1_B6, NULL);
	gpio_request(GPIO_FN_VI1_DATA5_VI1_B5, NULL);
	gpio_request(GPIO_FN_VI1_DATA4_VI1_B4, NULL);
	gpio_request(GPIO_FN_VI1_DATA3_VI1_B3, NULL);
	gpio_request(GPIO_FN_VI1_DATA2_VI1_B2, NULL);
	gpio_request(GPIO_FN_VI1_DATA1_VI1_B1, NULL);
	gpio_request(GPIO_FN_VI1_DATA0_VI1_B0, NULL);

#ifdef CONFIG_MACH_HURRICANE_VIN2
	/* VIN2 */
	gpio_request(GPIO_FN_VI2_CLK, NULL);
	gpio_request(GPIO_FN_VI2_DATA7_VI2_B7, NULL);
	gpio_request(GPIO_FN_VI2_DATA6_VI2_B6, NULL);
	gpio_request(GPIO_FN_VI2_DATA5_VI2_B5, NULL);
	gpio_request(GPIO_FN_VI2_DATA4_VI2_B4, NULL);
	gpio_request(GPIO_FN_VI2_DATA3_VI2_B3, NULL);
	gpio_request(GPIO_FN_VI2_DATA2_VI2_B2, NULL);
	gpio_request(GPIO_FN_VI2_DATA1_VI2_B1, NULL);
	gpio_request(GPIO_FN_VI2_DATA0_VI2_B0, NULL);
#endif

#ifndef CONFIG_MACH_HURRICANE_10BIT_CAMERAS
	/* VIN3 */
	gpio_request(GPIO_FN_VI3_CLK, NULL);
	gpio_request(GPIO_FN_VI3_DATA7, NULL);
	gpio_request(GPIO_FN_VI3_DATA6, NULL);
	gpio_request(GPIO_FN_VI3_DATA5, NULL);
	gpio_request(GPIO_FN_VI3_DATA4, NULL);
	gpio_request(GPIO_FN_VI3_DATA3, NULL);
	gpio_request(GPIO_FN_VI3_DATA2, NULL);
	gpio_request(GPIO_FN_VI3_DATA1, NULL);
	gpio_request(GPIO_FN_VI3_DATA0, NULL);
#endif /* !CONFIG_MACH_HURRICANE_10BIT_CAMERAS */


	/* On-Chip Ethernet */
	dev_set_drvdata(&rcar_eth_device.dev, &rcar_eth_plat);

	gpio_request(GPIO_FN_ETH_MDC, NULL);
	gpio_request(GPIO_FN_ETH_MDIO, NULL);
	gpio_request(GPIO_FN_ETH_TX_EN, NULL);
	gpio_request(GPIO_FN_ETH_RX_ER, NULL);
	gpio_request(GPIO_FN_ETH_RXD0, NULL);
	gpio_request(GPIO_FN_ETH_RXD1, NULL);
	gpio_request(GPIO_FN_ETH_TXD0, NULL);
	gpio_request(GPIO_FN_ETH_TXD1, NULL);
	gpio_request(GPIO_FN_ETH_LINK, NULL);
	gpio_request(GPIO_FN_ETH_CRS_DV, NULL);
	gpio_request(GPIO_FN_ETH_REFCLK, NULL);

#ifdef CONFIG_MACH_HURRICANE_DU1
	/* Display Unit 1 */
	gpio_request(GPIO_FN_DU1_DR7, NULL);
	gpio_request(GPIO_FN_DU1_DR6, NULL);
	gpio_request(GPIO_FN_DU1_DR5, NULL);
	gpio_request(GPIO_FN_DU1_DR4, NULL);
	gpio_request(GPIO_FN_DU1_DR3, NULL);
	gpio_request(GPIO_FN_DU1_DR2, NULL);
	gpio_request(GPIO_FN_DU1_DR1, NULL);
	gpio_request(GPIO_FN_DU1_DR0, NULL);
	gpio_request(GPIO_FN_DU1_DG7, NULL);
	gpio_request(GPIO_FN_DU1_DG6, NULL);
	gpio_request(GPIO_FN_DU1_DG5, NULL);
	gpio_request(GPIO_FN_DU1_DG4, NULL);
	gpio_request(GPIO_FN_DU1_DG3, NULL);
	gpio_request(GPIO_FN_DU1_DG2, NULL);
	gpio_request(GPIO_FN_DU1_DG1, NULL);
	gpio_request(GPIO_FN_DU1_DG0, NULL);
	gpio_request(GPIO_FN_DU1_DB7, NULL);
	gpio_request(GPIO_FN_DU1_DB6, NULL);
	gpio_request(GPIO_FN_DU1_DB5, NULL);
	gpio_request(GPIO_FN_DU1_DB4, NULL);
	gpio_request(GPIO_FN_DU1_DB3, NULL);
	gpio_request(GPIO_FN_DU1_DB2, NULL);
	gpio_request(GPIO_FN_DU1_DB1, NULL);
	gpio_request(GPIO_FN_DU1_DB0, NULL);
	gpio_request(GPIO_FN_DU1_EXVSYNC_DU1_VSYNC, NULL);
	gpio_request(GPIO_FN_DU1_EXHSYNC_DU1_HSYNC, NULL);
	gpio_request(GPIO_FN_DU1_DOTCLKOUT, NULL);
	gpio_request(GPIO_FN_DU1_DISP, NULL);
#endif

	r8a7779_add_standard_devices();
	platform_add_devices(hurricane_devices, ARRAY_SIZE(hurricane_devices));

	i2c_register_board_info(0, hurricane_i2c_devices,
				ARRAY_SIZE(hurricane_i2c_devices));

	spi_register_board_info(marzen_spi_devices,
		ARRAY_SIZE(marzen_spi_devices));

	rcar_usbh_init();
}

static int __init setup_rmac(char *s)
{
	printk(KERN_INFO "rmac = %s\n", s);
	parse_mac_addr(s);
	return 0;
}

__setup("rmac=", setup_rmac);

MACHINE_START(HURRICANE, "hurricane")
	.map_io		= r8a7779_map_io,
	.init_early	= r8a7779_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= r8a7779_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= hurricane_init,
	.timer		= &shmobile_timer,
MACHINE_END
