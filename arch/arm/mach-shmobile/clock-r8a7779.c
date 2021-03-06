/*
 * r8a7779 clock framework support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <mach/common.h>

#define FRQMR   0xffc80014
#define MSTPCR0 0xffc80030
#define MSTPCR1 0xffc80034
#define MSTPCR3 0xffc8003c
#define MSTPSR1 0xffc80044
#define MSTPSR4 0xffc80048
#define MSTPSR6 0xffc8004c
#define MSTPCR4 0xffc80050
#define MSTPCR5 0xffc80054
#define MSTPCR6 0xffc80058
#define MSTPCR7 0xffc80040

/* ioremap() through clock mapping mandatory to avoid
 * collision with ARM coherent DMA virtual memory range.
 */

static struct clk_mapping cpg_mapping = {
	.phys	= 0xffc80000,
	.len	= 0x80,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk plla_clk = {
	.rate		= 1500000000,
	.mapping	= &cpg_mapping,
};

static struct clk *main_clks[] = {
	&plla_clk,
};

static int divisors[] = { 0, 0, 0, 6, 8, 12, 16, 0, 24, 32, 36, 0, 0, 0, 0, 0 };

static struct clk_div_mult_table div4_div_mult_table = {
	.divisors = divisors,
	.nr_divisors = ARRAY_SIZE(divisors),
};

static struct clk_div4_table div4_table = {
	.div_mult_table = &div4_div_mult_table,
};

enum { DIV4_S, DIV4_OUT, DIV4_S4, DIV4_S3, DIV4_S1, DIV4_P, DIV4_NR };

static struct clk div4_clks[DIV4_NR] = {
	[DIV4_S]	= SH_CLK_DIV4(&plla_clk, FRQMR, 20,
				      0x0018, CLK_ENABLE_ON_INIT),
	[DIV4_OUT]	= SH_CLK_DIV4(&plla_clk, FRQMR, 16,
				      0x0700, CLK_ENABLE_ON_INIT),
	[DIV4_S4]	= SH_CLK_DIV4(&plla_clk, FRQMR, 12,
				      0x0040, CLK_ENABLE_ON_INIT),
	[DIV4_S3]	= SH_CLK_DIV4(&plla_clk, FRQMR, 8,
				      0x0010, CLK_ENABLE_ON_INIT),
	[DIV4_S1]	= SH_CLK_DIV4(&plla_clk, FRQMR, 4,
				      0x0060, CLK_ENABLE_ON_INIT),
	[DIV4_P]	= SH_CLK_DIV4(&plla_clk, FRQMR, 0,
				      0x0300, CLK_ENABLE_ON_INIT),
};

enum { MSTP331, MSTP330,
	MSTP323, MSTP322, MSTP321, MSTP320, MSTP312, MSTP311, MSTP310, MSTP309,
	MSTP308, MSTP307,
	MSTP120, MSTP116, MSTP115, MSTP114, MSTP110, MSTP109,
	MSTP108, MSTP105, MSTP103, MSTP101, MSTP100,
	MSTP030, MSTP029, MSTP028, MSTP027, MSTP026, MSTP025, MSTP024, MSTP023,
	MSTP022, MSTP021, MSTP016,
	MSTP015, MSTP014, MSTP012, MSTP011, MSTP010, MSTP009, MSTP008, MSTP007,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP331] = SH_CLK_MSTP32(&div4_clks[DIV4_S4], MSTPCR3, 31, 0), /* MMC0 */
	[MSTP330] = SH_CLK_MSTP32(&div4_clks[DIV4_S4], MSTPCR3, 30, 0), /* MMC1 */
	[MSTP323] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 23, 0), /* SDHI0 */
	[MSTP322] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 22, 0), /* SDHI1 */
	[MSTP321] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 21, 0), /* SDHI2 */
	[MSTP320] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 20, 0), /* SDHI3 */
	[MSTP312] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 12, 0), /* SSI4 */
	[MSTP311] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 11, 0), /* SSI5 */
	[MSTP310] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 10, 0), /* SSI6 */
	[MSTP309] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 9, 0), /* SSI7 */
	[MSTP308] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 8, 0), /* SSI8 */
	[MSTP307] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR3, 7, 0), /* SSI9 */
	[MSTP120] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1, 20, 0), /* VIN3 */
	[MSTP116] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1, 16, 0), /* PCIe */
	[MSTP115] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1, 15, 0), /* SATA */
	[MSTP114] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1, 14, CLK_ENABLE_ON_INIT), /* ETH */
	[MSTP110] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1, 10, 0), /* VIN0 */
	[MSTP109] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1,  9, 0), /* VIN1 */
	[MSTP108] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1,  8, 0), /* VIN2 */
	[MSTP105] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1,  5, 0), /* SGX */
	[MSTP103] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR1,  3, 0), /* DU */
	[MSTP101] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1,  1, 0), /* USB2 */
	[MSTP100] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR1,  0, 0), /* USB01 */
	[MSTP030] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 30, 0), /* I2C0 */
	[MSTP029] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 29, 0), /* I2C1 */
	[MSTP028] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 28, 0), /* I2C2 */
	[MSTP027] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 27, 0), /* I2C3 */
	[MSTP026] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 26, 0), /* SCIF0 */
	[MSTP025] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 25, 0), /* SCIF1 */
	[MSTP024] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 24, 0), /* SCIF2 */
	[MSTP023] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 23, 0), /* SCIF3 */
	[MSTP022] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 22, 0), /* SCIF4 */
	[MSTP021] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 21, 0), /* SCIF5 */
	[MSTP016] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 16, 0), /* TMU0 */
	[MSTP015] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 15, 0), /* TMU1 */
	[MSTP014] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 14, 0), /* TMU2 */
	[MSTP012] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 12, 0), /* SSI0 */
	[MSTP011] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 11, 0), /* SSI1 */
	[MSTP010] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 10, 0), /* SSI2 */
	[MSTP009] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 9, 0), /* SSI3 */
	[MSTP008] = SH_CLK_MSTP32(&div4_clks[DIV4_P], MSTPCR0, 8, 0), /* SRU */
	[MSTP007] = SH_CLK_MSTP32(&div4_clks[DIV4_S], MSTPCR0,  7, 0), /* HSPI */
};

static unsigned long mul4_recalc(struct clk *clk)
{
	return clk->parent->rate * 4;
}

static struct sh_clk_ops mul4_clk_ops = {
	.recalc		= mul4_recalc,
};

struct clk clkz_clk = {
	.ops		= &mul4_clk_ops,
	.parent		= &div4_clks[DIV4_S],
};

struct clk clkzs_clk = {
	/* clks x 4 / 4 = clks */
	.parent		= &div4_clks[DIV4_S],
};

static struct clk *late_main_clks[] = {
	&clkz_clk,
	&clkzs_clk,
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("plla_clk", &plla_clk),
	CLKDEV_CON_ID("clkz_clk", &clkz_clk),
	CLKDEV_CON_ID("clkzs_clk", &clkzs_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("shyway_clk",	&div4_clks[DIV4_S]),
	CLKDEV_CON_ID("bus_clk",	&div4_clks[DIV4_OUT]),
	CLKDEV_CON_ID("shyway4_clk",	&div4_clks[DIV4_S4]),
	CLKDEV_CON_ID("shyway3_clk",	&div4_clks[DIV4_S3]),
	CLKDEV_CON_ID("shyway1_clk",	&div4_clks[DIV4_S1]),
	CLKDEV_CON_ID("peripheral_clk",	&div4_clks[DIV4_P]),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("sh_tmu.0", &mstp_clks[MSTP016]), /* TMU00 */
	CLKDEV_DEV_ID("sh_tmu.1", &mstp_clks[MSTP016]), /* TMU01 */
	CLKDEV_DEV_ID("i2c-rcar.0", &mstp_clks[MSTP030]), /* I2C0 */
	CLKDEV_DEV_ID("i2c-rcar.1", &mstp_clks[MSTP029]), /* I2C1 */
	CLKDEV_DEV_ID("i2c-rcar.2", &mstp_clks[MSTP028]), /* I2C2 */
	CLKDEV_DEV_ID("i2c-rcar.3", &mstp_clks[MSTP027]), /* I2C3 */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP026]), /* SCIF0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP025]), /* SCIF1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP024]), /* SCIF2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP023]), /* SCIF3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP022]), /* SCIF4 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP021]), /* SCIF6 */
	CLKDEV_DEV_ID("sh-hspi.0", &mstp_clks[MSTP007]), /* HSPI0 */
	CLKDEV_DEV_ID("sh-hspi.1", &mstp_clks[MSTP007]), /* HSPI1 */
	CLKDEV_DEV_ID("sh-hspi.2", &mstp_clks[MSTP007]), /* HSPI2 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP323]), /* SDHI0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP322]), /* SDHI1 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP321]), /* SDHI2 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.3", &mstp_clks[MSTP320]), /* SDHI3 */
	CLKDEV_DEV_ID("sh-eth", &mstp_clks[MSTP114]),
	CLKDEV_DEV_ID("rcar_vin.3", &mstp_clks[MSTP120]), /* VIN3 */
	CLKDEV_CON_ID("pciec", &mstp_clks[MSTP116]), /* PCIe */
	CLKDEV_CON_ID("sata", &mstp_clks[MSTP115]), /* SATA */
	CLKDEV_DEV_ID("rcar_vin.0", &mstp_clks[MSTP110]), /* VIN0 */
	CLKDEV_DEV_ID("rcar_vin.1", &mstp_clks[MSTP109]), /* VIN1 */
	CLKDEV_DEV_ID("rcar_vin.2", &mstp_clks[MSTP108]), /* VIN2 */
	CLKDEV_CON_ID("sgx", &mstp_clks[MSTP105]), /* SGX */
	CLKDEV_DEV_ID("rcarfb.0", &mstp_clks[MSTP103]), /* DU0 */
	CLKDEV_DEV_ID("rcarfb.1", &mstp_clks[MSTP103]), /* DU1 */

	CLKDEV_CON_ID("mmc0", &mstp_clks[MSTP331]), /* MMC0 */
	CLKDEV_CON_ID("mmc1", &mstp_clks[MSTP330]), /* MMC1 */

	CLKDEV_CON_ID("usb_fck", &mstp_clks[MSTP100]),
	CLKDEV_CON_ID("usb_fck2", &mstp_clks[MSTP101]),

	CLKDEV_CON_ID("ssi0", &mstp_clks[MSTP012]),
	CLKDEV_CON_ID("ssi1", &mstp_clks[MSTP011]),
	CLKDEV_CON_ID("ssi2", &mstp_clks[MSTP010]),
	CLKDEV_CON_ID("ssi3", &mstp_clks[MSTP009]),
	CLKDEV_CON_ID("ssi4", &mstp_clks[MSTP312]),
	CLKDEV_CON_ID("ssi5", &mstp_clks[MSTP311]),
	CLKDEV_CON_ID("ssi6", &mstp_clks[MSTP310]),
	CLKDEV_CON_ID("ssi7", &mstp_clks[MSTP309]),
	CLKDEV_CON_ID("ssi8", &mstp_clks[MSTP308]),
	CLKDEV_CON_ID("ssi9", &mstp_clks[MSTP307]),
	CLKDEV_CON_ID("sru", &mstp_clks[MSTP008]),
};

void __init r8a7779_clock_init(void)
{
	int k, ret = 0;

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_div4_register(div4_clks, DIV4_NR, &div4_table);

	if (!ret)
		ret = sh_clk_mstp32_register(mstp_clks, MSTP_NR);

	for (k = 0; !ret && (k < ARRAY_SIZE(late_main_clks)); k++)
		ret = clk_register(late_main_clks[k]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7779 clocks\n");
}
