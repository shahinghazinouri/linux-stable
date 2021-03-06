/*
 * drivers/ata/sata_rcar.c
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Some parts based on libata-sff.c by Jeff Garzik and Red Hat, Inc.
 * Some parts based on sata_mv.c by Red Hat, Inc. and others.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <linux/libata.h>

#define DRV_NAME	"sata_rcar"
#define DRV_VERSION	"0.01"

enum {
	/* control register offsets */
	RC_ATAPI_CONTROL1	= 0x0180,
	RC_ATAPI_STATUS		= 0x0184,
	RC_ATAPI_INT_ENABLE	= 0x0188,
	RC_ATAPI_DTB_ADR	= 0x0198,
	RC_ATAPI_DMA_START_ADR	= 0x019C,
	RC_ATAPI_DMA_TRANS_CNT	= 0x01A0,
	RC_ATAPI_CONTROL2	= 0x01A4,
	RC_ATAPI_SIG_ST		= 0x01B0,
	RC_ATAPI_BYTE_SWAP	= 0x01BC,

	/* phy register offsets */
	RC_SATAPHYADRR		= 0x0200,
	RC_SATAPHYWDATA		= 0x0204,
	RC_SATAPHYACCEN		= 0x0208,
	RC_SATAPHYRESET		= 0x020C,
	RC_SATAPHYRDATA		= 0x0210,
	RC_SATAPHYACK		= 0x0214,

	/* host register offsets */
	RC_BISTCONF		= 0x102C,
	RC_SDATA		= 0x1100,
	RC_SSSERR		= 0x1104,
	RC_SSFEATURES		= 0x1104,
	RC_SSECCNT		= 0x1108,
	RC_SLBALOW		= 0x110C,
	RC_SLBAMID		= 0x1110,
	RC_SLBAHIGH		= 0x1114,
	RC_SDEVHEAD		= 0x1118,
	RC_SSSTATUS		= 0x111C,
	RC_SSCOM		= 0x111C,
	RC_SSALTSTS		= 0x1204,
	RC_SSDEVCON		= 0x1204,
	RC_SCR_OFS		= 0x1400,
	RC_SATAINTSTAT		= 0x1508,
	RC_SATAINTMASK		= 0x150C,
	RC_DMADW_OFS		= 0x1620,

	/* register bit field */
	RC_ATAPI_CONTROL1_ISM		= (1 << 16),
	RC_ATAPI_CONTROL1_DTA32M	= (1 << 11),
	RC_ATAPI_CONTROL1_RESET		= (1 << 7),
	RC_ATAPI_CONTROL1_DESE		= (1 << 3),
	RC_ATAPI_CONTROL1_RW		= (1 << 2),
	RC_ATAPI_CONTROL1_STOP		= (1 << 1),
	RC_ATAPI_CONTROL1_START		= (1 << 0),
	RC_ATAPI_CONTROL1_STANDARD	= RC_ATAPI_CONTROL1_ISM |
					  RC_ATAPI_CONTROL1_DTA32M |
					  RC_ATAPI_CONTROL1_DESE,
	RC_ATAPI_STATUS_SSERR		= (1 << 24),
	RC_ATAPI_STATUS_SATAINT		= (1 << 11),
	RC_ATAPI_STATUS_SWERR		= (1 << 8),
	RC_ATAPI_STATUS_DNEND		= (1 << 6),
	RC_ATAPI_STATUS_ERR		= (1 << 2),
	RC_ATAPI_STATUS_ACT		= (1 << 0),
	RC_ATAPI_STATUS_DMAERR		= RC_ATAPI_STATUS_SWERR |
					  RC_ATAPI_STATUS_ERR,
	RC_ATAPI_INT_ENABLE_SATAINT	= (1 << 11),
	RC_ATAPI_INT_ENABLE_SWERR	= (1 << 8),
	RC_ATAPI_INT_ENABLE_ERR		= (1 << 2),
	RC_ATAPI_INT_ENABLE_SATA	= RC_ATAPI_INT_ENABLE_SWERR |
					  RC_ATAPI_INT_ENABLE_ERR |
					  RC_ATAPI_INT_ENABLE_SATAINT,
	RC_PRD_DTEND			= (1 << 0),	/* at DTA32M=1 */
};

struct rc_host_priv {
	void __iomem		*base;
	bool			incomplete;
#if defined(CONFIG_HAVE_CLK)
	struct clk		*clk;
#endif
};

static void rcar_ata_bmdma_qc_prep(struct ata_queued_cmd *qc);
static void rcar_ata_sff_freeze(struct ata_port *ap);
static void rcar_ata_sff_thaw(struct ata_port *ap);
static int rcar_ata_sff_softreset(struct ata_link *link, unsigned int *classes,
		unsigned long deadline);
static int rcar_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val);
static int rcar_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val);
static void rcar_port_stop(struct ata_port *ap);
static void rcar_ata_sff_dev_select(struct ata_port *ap, unsigned int device);
static void rcar_ata_sff_set_devctl(struct ata_port *ap, u8 ctl);
static u8 rcar_ata_sff_check_status(struct ata_port *ap);
static u8 rcar_ata_sff_altstatus(struct ata_port *ap);
static void rcar_ata_sff_tf_load(struct ata_port *ap,
		const struct ata_taskfile *tf);
static void rcar_ata_sff_tf_read(struct ata_port *ap, struct ata_taskfile *tf);
static void rcar_ata_sff_exec_command(struct ata_port *ap,
		const struct ata_taskfile *tf);
static unsigned int rcar_ata_sff_data_xfer(struct ata_device *dev,
		unsigned char *buf, unsigned int buflen, int rw);
static void rcar_ata_bmdma_irq_clear(struct ata_port *ap);
static void rcar_ata_sff_drain_fifo(struct ata_queued_cmd *qc);
static void rcar_ata_bmdma_setup(struct ata_queued_cmd *qc);
static void rcar_ata_bmdma_start(struct ata_queued_cmd *qc);
static void rcar_ata_bmdma_stop(struct ata_queued_cmd *qc);
static u8 rcar_ata_bmdma_status(struct ata_port *ap);

static struct scsi_host_template rc_ata_sht = {
	ATA_BMDMA_SHT(DRV_NAME),
};

static struct ata_port_operations rc_ata_ops = {
	.inherits		= &ata_bmdma_port_ops,

	/* Command execution */
	.qc_prep		= rcar_ata_bmdma_qc_prep,

	/* Configuration and exception handling */
	.freeze			= rcar_ata_sff_freeze,
	.thaw			= rcar_ata_sff_thaw,
	.softreset		= rcar_ata_sff_softreset,
	.hardreset		= sata_std_hardreset,
	.lost_interrupt		= ATA_OP_NULL,

	/* Optional features */
	.scr_read		= rcar_scr_read,
	.scr_write		= rcar_scr_write,

	/* Start, stop, suspend and resume */
	.port_stop		= rcar_port_stop,

	/* SFF / taskfile oriented ops */
	.sff_dev_select		= rcar_ata_sff_dev_select,
	.sff_set_devctl		= rcar_ata_sff_set_devctl,
	.sff_check_status	= rcar_ata_sff_check_status,
	.sff_check_altstatus	= rcar_ata_sff_altstatus,
	.sff_tf_load		= rcar_ata_sff_tf_load,
	.sff_tf_read		= rcar_ata_sff_tf_read,
	.sff_exec_command	= rcar_ata_sff_exec_command,
	.sff_data_xfer		= rcar_ata_sff_data_xfer,
	.sff_irq_clear		= rcar_ata_bmdma_irq_clear,
	.sff_drain_fifo		= rcar_ata_sff_drain_fifo,
	.bmdma_setup		= rcar_ata_bmdma_setup,
	.bmdma_start		= rcar_ata_bmdma_start,
	.bmdma_stop		= rcar_ata_bmdma_stop,
	.bmdma_status		= rcar_ata_bmdma_status,
};

static const struct ata_port_info rc_port_info[] = {
	{
		.flags		= ATA_FLAG_SATA,
		.pio_mask	= ATA_PIO4,
		.udma_mask	= ATA_UDMA6,
		.port_ops	= &rc_ata_ops,
	},
};

static inline void __iomem *rcar_host_base(struct ata_host *host)
{
	struct rc_host_priv *hpriv = host->private_data;
	return hpriv->base;
}

static inline void __iomem *rcar_ap_base(struct ata_port *ap)
{
	return rcar_host_base(ap->host);
}

/**
 *	rcar_ata_bmdma_fill_sg - Fill IDE PRD table
 *	@qc: Metadata associated with taskfile to be transferred
 *
 *	Fill IDE PRD (scatter-gather) table with segments
 *	associated with the current disk command.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 *
 */
static void rcar_ata_bmdma_fill_sg(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct rc_host_priv *hpriv = ap->host->private_data;
	struct ata_bmdma_prd *prd = ap->bmdma_prd;
	struct scatterlist *sg;
	unsigned int si, pi;

	hpriv->incomplete = 0;

	pi = 0;
	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		u32 addr, offset;
		u32 sg_len, len;

		/* determine if physical DMA addr spans 64K boundary.
		 * Note h/w doesn't support 64-bit, so we unconditionally
		 * truncate dma_addr_t to u32.
		 */
		addr = (u32) sg_dma_address(sg);
		sg_len = sg_dma_len(sg);

		while (sg_len) {
			offset = addr & 0xffff;
			len = sg_len;
			if ((offset + sg_len) > 0x10000)
				len = 0x10000 - offset;

			prd[pi].addr = cpu_to_le32(addr);
			prd[pi].flags_len = cpu_to_le32(len);
			VPRINTK("PRD[%u] = (0x%X, 0x%X)\n", pi, addr, len);

			pi++;
			sg_len -= len;
			addr += len;

			/* if transfer size is littler than sector size,
			 * the DMA transfer will not finish.
			 */
			if (len < ATA_SECT_SIZE)
				hpriv->incomplete = 1;
		}
	}

	prd[pi - 1].addr |= RC_PRD_DTEND;	/* mark the end of table */
}

/**
 *	rcar_bmdma_ata_qc_prep - Prepare taskfile for submission
 *	@qc: Metadata associated with taskfile to be prepared
 *
 *	Prepare ATA taskfile for submission.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_bmdma_qc_prep(struct ata_queued_cmd *qc)
{
	if (!(qc->flags & ATA_QCFLAG_DMAMAP))
		return;

	rcar_ata_bmdma_fill_sg(qc);
}

/**
 *	rcar_ata_sff_wait_after_reset - wait for devices to become ready after
 *	reset
 *	@link: SFF link which is just reset
 *	@devmask: mask of present devices
 *	@deadline: deadline jiffies for the operation
 *
 *	Wait devices attached to SFF @link to become ready after
 *	reset.  It contains preceding 150ms wait to avoid accessing TF
 *	status register too early.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -ENODEV if some or all of devices in @devmask
 *	don't seem to exist.  -errno on other errors.
 */
static int rcar_ata_sff_wait_after_reset(struct ata_link *link,
			unsigned int devmask, unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	int rc;

	msleep(ATA_WAIT_AFTER_RESET);

	/* always check readiness of the master device */
	rc = ata_sff_wait_ready(link, deadline);
	/* -ENODEV means the odd clown forgot the D7 pulldown resistor
	 * and TF status is 0xff, bail out on it too.
	 */
	if (rc)
		return rc;

	/* is all this really necessary? */
	rcar_ata_sff_dev_select(ap, 0);

	return 0;
}

static int rcar_ata_bus_softreset(struct ata_port *ap, unsigned int devmask,
			unsigned long deadline)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	DPRINTK("ata%u: bus reset via SRST\n", ap->print_id);

	/* software reset.  causes dev0 to be selected */
	writel(ap->ctl, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	writel(ap->ctl | ATA_SRST, ioaddr->ctl_addr);
	udelay(20);	/* FIXME: flush */
	writel(ap->ctl, ioaddr->ctl_addr);
	ap->last_ctl = ap->ctl;

	/* wait the port to become ready */
	return rcar_ata_sff_wait_after_reset(&ap->link, devmask, deadline);
}

/**
 *	rcar_ata_devchk - PATA device presence detection
 *	@ap: ATA channel to examine
 *	@device: Device to examine (starting at zero)
 *
 *	This technique was originally described in
 *	Hale Landis's ATADRVR (www.ata-atapi.com), and
 *	later found its way into the ATA/ATAPI spec.
 *
 *	Write a pattern to the ATA shadow registers,
 *	and if a device is present, it will respond by
 *	correctly storing and echoing back the
 *	ATA shadow register contents.
 *
 *	LOCKING:
 *	caller.
 */
static unsigned int rcar_ata_devchk(struct ata_port *ap, unsigned int device)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	u8 nsect, lbal;

	if (device != 0)	/* support device 0 only */
		return 0;

	rcar_ata_sff_dev_select(ap, device);

	writel(0x55, ioaddr->nsect_addr);
	writel(0xaa, ioaddr->lbal_addr);

	writel(0xaa, ioaddr->nsect_addr);
	writel(0x55, ioaddr->lbal_addr);

	writel(0x55, ioaddr->nsect_addr);
	writel(0xaa, ioaddr->lbal_addr);

	nsect = readl(ioaddr->nsect_addr);
	lbal = readl(ioaddr->lbal_addr);

	if ((nsect == 0x55) && (lbal == 0xaa))
		return 1;	/* we found a device */

	return 0;		/* nothing found */
}

/**
 *	rcar_ata_sff_freeze - Freeze SFF controller port
 *	@ap: port to freeze
 *
 *	Freeze SFF controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void rcar_ata_sff_freeze(struct ata_port *ap)
{
	void __iomem *base = rcar_ap_base(ap);

	ap->ctl |= ATA_NIEN;
	ap->last_ctl = ap->ctl;

	rcar_ata_sff_set_devctl(ap, ap->ctl);

	/* Under certain circumstances, some controllers raise IRQ on
	 * ATA_NIEN manipulation.  Also, many controllers fail to mask
	 * previously pending IRQ on ATA_NIEN assertion.  Clear it.
	 */
	rcar_ata_sff_check_status(ap);
	rcar_ata_bmdma_irq_clear(ap);

	/* disable interrupts */
	writel(0, base + RC_ATAPI_INT_ENABLE);
}
/**
 *	rcar_ata_sff_thaw - Thaw SFF controller port
 *	@ap: port to thaw
 *
 *	Thaw SFF controller port.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void rcar_ata_sff_thaw(struct ata_port *ap)
{
	void __iomem *base = rcar_ap_base(ap);

	/* clear & re-enable interrupts */
	rcar_ata_sff_check_status(ap);
	rcar_ata_bmdma_irq_clear(ap);

	writel(RC_ATAPI_INT_ENABLE_SATA, base + RC_ATAPI_INT_ENABLE);
	writel(0x000001F0, base + RC_SATAINTMASK);
}

/**
 *	rcar_ata_sff_softreset - reset host port via ATA SRST
 *	@link: ATA link to reset
 *	@classes: resulting classes of attached devices
 *	@deadline: deadline jiffies for the operation
 *
 *	Reset host port using ATA SRST.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep)
 *
 *	RETURNS:
 *	0 on success, -errno otherwise.
 */
static int rcar_ata_sff_softreset(struct ata_link *link, unsigned int *classes,
		unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	unsigned int devmask = 0;
	int rc;
	u8 err;

	/* determine if device 0/1 are present */
	if (rcar_ata_devchk(ap, 0))
		devmask |= (1 << 0);

	/* select device 0 again */
	rcar_ata_sff_dev_select(ap, 0);

	/* issue bus reset */
	rc = rcar_ata_bus_softreset(ap, devmask, deadline);
	/* if link is occupied, -ENODEV too is an error */
	if (rc && (rc != -ENODEV || sata_scr_valid(link))) {
		ata_link_printk(link, KERN_ERR, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	/* determine by signature whether we have ATA or ATAPI devices */
	classes[0] = ata_sff_dev_classify(&link->device[0],
					  devmask & (1 << 0), &err);

	return 0;
}

static int rcar_scr_read(struct ata_link *link, unsigned int sc_reg, u32 *val)
{
	void __iomem *base = rcar_ap_base(link->ap);

	if (sc_reg >= SCR_NOTIFICATION)
		return -EINVAL;

	*val = readl(base + RC_SCR_OFS + (sc_reg * 4));
	return 0;
}

static int rcar_scr_write(struct ata_link *link, unsigned int sc_reg, u32 val)
{
	void __iomem *base = rcar_ap_base(link->ap);

	if (sc_reg >= SCR_NOTIFICATION)
		return -EINVAL;

	writel(val, base + RC_SCR_OFS + (sc_reg * 4));
	return 0;
}

static void rcar_port_stop(struct ata_port *ap)
{
	void __iomem *base = rcar_ap_base(ap);
	u32 hostctl;

	/* stop DMA */
	hostctl = RC_ATAPI_CONTROL1_STANDARD | RC_ATAPI_CONTROL1_STOP;
	writel(hostctl, base + RC_ATAPI_CONTROL1);
	/* disable interrupts */
	writel(0, base + RC_ATAPI_INT_ENABLE);
}

/**
 *	rcar_ata_sff_dev_select - Select device 0/1 on ATA bus
 *	@ap: ATA channel to manipulate
 *	@device: ATA device (numbered from zero) to select
 *
 *	Use the method defined in the ATA specification to
 *	make either device 0, or device 1, active on the
 *	ATA channel.  Works with both PIO and MMIO.
 *
 *	LOCKING:
 *	caller.
 */
static void rcar_ata_sff_dev_select(struct ata_port *ap, unsigned int device)
{
	/* support device 0 only */
	writel(ATA_DEVICE_OBS, ap->ioaddr.device_addr);
	ata_sff_pause(ap);	/* needed; also flushes, for mmio */
}

/**
 *	rcar_ata_sff_set_devctl - Write device control reg
 *	@ap: port where the device is
 *	@ctl: value to write
 *
 *	Writes ATA taskfile device control register.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void rcar_ata_sff_set_devctl(struct ata_port *ap, u8 ctl)
{
	writel(ctl, ap->ioaddr.ctl_addr);
}

/**
 *	rcar_ata_sff_check_status - Read device status reg & clear interrupt
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile status register for currently-selected device
 *	and return its value. This also clears pending interrupts
 *      from this device
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 rcar_ata_sff_check_status(struct ata_port *ap)
{
	return (u8)readl(ap->ioaddr.status_addr);
}

/**
 *	rcar_ata_sff_altstatus - Read device alternate status reg
 *	@ap: port where the device is
 *
 *	Reads ATA taskfile alternate status register for
 *	currently-selected device and return its value.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static u8 rcar_ata_sff_altstatus(struct ata_port *ap)
{
	return (u8)readl(ap->ioaddr.altstatus_addr);
}

/**
 *	rcar_ata_sff_tf_load - send taskfile registers to host controller
 *	@ap: Port to which output is sent
 *	@tf: ATA taskfile register set
 *
 *	Outputs ATA taskfile to standard ATA host controller.
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void rcar_ata_sff_tf_load(struct ata_port *ap,
		const struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;
	unsigned int is_addr = tf->flags & ATA_TFLAG_ISADDR;

	if (tf->ctl != ap->last_ctl) {
		if (ioaddr->ctl_addr)
			writel(tf->ctl, ioaddr->ctl_addr);
		ap->last_ctl = tf->ctl;
		ata_wait_idle(ap);
	}

	if (is_addr && (tf->flags & ATA_TFLAG_LBA48)) {
		WARN_ON_ONCE(!ioaddr->ctl_addr);
		writel(tf->hob_feature, ioaddr->feature_addr);
		writel(tf->hob_nsect, ioaddr->nsect_addr);
		writel(tf->hob_lbal, ioaddr->lbal_addr);
		writel(tf->hob_lbam, ioaddr->lbam_addr);
		writel(tf->hob_lbah, ioaddr->lbah_addr);
		VPRINTK("hob: feat 0x%X nsect 0x%X, lba 0x%X 0x%X 0x%X\n",
			tf->hob_feature,
			tf->hob_nsect,
			tf->hob_lbal,
			tf->hob_lbam,
			tf->hob_lbah);
	}

	if (is_addr) {
		writel(tf->feature, ioaddr->feature_addr);
		writel(tf->nsect, ioaddr->nsect_addr);
		writel(tf->lbal, ioaddr->lbal_addr);
		writel(tf->lbam, ioaddr->lbam_addr);
		writel(tf->lbah, ioaddr->lbah_addr);
		VPRINTK("feat 0x%X nsect 0x%X lba 0x%X 0x%X 0x%X\n",
			tf->feature,
			tf->nsect,
			tf->lbal,
			tf->lbam,
			tf->lbah);
	}

	if (tf->flags & ATA_TFLAG_DEVICE) {
		writel(tf->device, ioaddr->device_addr);
		VPRINTK("device 0x%X\n", tf->device);
	}

	ata_wait_idle(ap);
}

/**
 *	rcar_ata_sff_tf_read - input device's ATA taskfile shadow registers
 *	@ap: Port from which input is read
 *	@tf: ATA taskfile register set for storing input
 *
 *	Reads ATA taskfile registers for currently-selected device
 *	into @tf. Assumes the device has a fully SFF compliant task file
 *	layout and behaviour. If you device does not (eg has a different
 *	status method) then you will need to provide a replacement tf_read
 *
 *	LOCKING:
 *	Inherited from caller.
 */
static void rcar_ata_sff_tf_read(struct ata_port *ap, struct ata_taskfile *tf)
{
	struct ata_ioports *ioaddr = &ap->ioaddr;

	tf->command = (u8)readl(ioaddr->status_addr);
	tf->feature = (u8)readl(ioaddr->error_addr);
	tf->nsect = (u8)readl(ioaddr->nsect_addr);
	tf->lbal = (u8)readl(ioaddr->lbal_addr);
	tf->lbam = (u8)readl(ioaddr->lbam_addr);
	tf->lbah = (u8)readl(ioaddr->lbah_addr);
	tf->device = (u8)readl(ioaddr->device_addr);

	if (tf->flags & ATA_TFLAG_LBA48) {
		if (likely(ioaddr->ctl_addr)) {
			writel(tf->ctl | ATA_HOB, ioaddr->ctl_addr);
			tf->hob_feature = (u8)readl(ioaddr->error_addr);
			tf->hob_nsect = (u8)readl(ioaddr->nsect_addr);
			tf->hob_lbal = (u8)readl(ioaddr->lbal_addr);
			tf->hob_lbam = (u8)readl(ioaddr->lbam_addr);
			tf->hob_lbah = (u8)readl(ioaddr->lbah_addr);
			writel(tf->ctl, ioaddr->ctl_addr);
			ap->last_ctl = tf->ctl;
		} else
			WARN_ON_ONCE(1);
	}
}

/**
 *	rcar_ata_sff_exec_command - issue ATA command to host controller
 *	@ap: port to which command is being issued
 *	@tf: ATA taskfile register set
 *
 *	Issues ATA command, with proper synchronization with interrupt
 *	handler / other threads.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_sff_exec_command(struct ata_port *ap,
		const struct ata_taskfile *tf)
{
	DPRINTK("ata%u: cmd 0x%X\n", ap->print_id, tf->command);

	writel(tf->command, ap->ioaddr.command_addr);
	ata_sff_pause(ap);
}

static void rcar_ioread16_rep(void __iomem *addr, u16 *buf, unsigned int c)
{
	unsigned int i;

	for (i = 0; i < c; i++)
		buf[i] = (u16)readl(addr);
}

static void rcar_iowrite16_rep(void __iomem *addr, u16 *buf, unsigned int c)
{
	unsigned int i;

	for (i = 0; i < c; i++)
		writel(buf[i], addr);
}

/**
 *	rcar_ata_sff_data_xfer - Transfer data by PIO
 *	@dev: device to target
 *	@buf: data buffer
 *	@buflen: buffer length
 *	@rw: read/write
 *
 *	Transfer data from/to the device data register by PIO.
 *
 *	LOCKING:
 *	Inherited from caller.
 *
 *	RETURNS:
 *	Bytes consumed.
 */
static unsigned int rcar_ata_sff_data_xfer(struct ata_device *dev,
		unsigned char *buf, unsigned int buflen, int rw)
{
	struct ata_port *ap = dev->link->ap;
	void __iomem *data_addr = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;

	/* Transfer multiple of 2 bytes */
	if (rw == READ)
		rcar_ioread16_rep(data_addr, (u16 *)buf, words);
	else
		rcar_iowrite16_rep(data_addr, (u16 *)buf, words);

	/* Transfer trailing byte, if any. */
	if (unlikely(buflen & 0x01)) {
		unsigned char pad[2];

		/* Point buf to the tail of buffer */
		buf += buflen - 1;

		/*
		 * Use io*16_rep() accessors here as well to avoid pointlessly
		 * swapping bytes to and from on the big endian machines...
		 */
		if (rw == READ) {
			rcar_ioread16_rep(data_addr, (u16 *)pad, 1);
			*buf = pad[0];
		} else {
			pad[0] = *buf;
			rcar_iowrite16_rep(data_addr, (u16 *)pad, 1);
		}
		words++;
	}

	return words << 1;
}

/**
 *	rcar_ata_bmdma_irq_clear - Clear IDE BMDMA interrupt.
 *	@ap: Port associated with this ATA transaction.
 *
 *	Clear interrupt and error flags in DMA status register.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_bmdma_irq_clear(struct ata_port *ap)
{
	void __iomem *base = rcar_ap_base(ap);

	/* clear DMA status */
	writel(0, base + RC_SATAINTSTAT);
	writel(0, base + RC_ATAPI_STATUS);
}

/**
 *	rcar_ata_sff_drain_fifo - Stock FIFO drain logic for SFF controllers
 *	@qc: command
 *
 *	Drain the FIFO and device of any stuck data following a command
 *	failing to complete. In some cases this is necessary before a
 *	reset will recover the device.
 *
 */
static void rcar_ata_sff_drain_fifo(struct ata_queued_cmd *qc)
{
	int count;
	struct ata_port *ap;

	/* We only need to flush incoming data when a command was running */
	if (qc == NULL || qc->dma_dir == DMA_TO_DEVICE)
		return;

	ap = qc->ap;
	/* Drain up to 64K of data before we give up this recovery method */
	for (count = 0; (rcar_ata_sff_check_status(ap) & ATA_DRQ)
						&& count < 65536; count += 2)
		readl(ap->ioaddr.data_addr);

	/* Can become DEBUG later */
	if (count)
		ata_port_printk(ap, KERN_DEBUG,
			"drained %d bytes to clear DRQ.\n", count);

}

/**
 *	rcar_ata_bmdma_setup - Set up IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	unsigned int rw = (qc->tf.flags & ATA_TFLAG_WRITE);
	void __iomem *base = rcar_ap_base(ap);
	u32 hostctl;

	/* load PRD table addr. */
	mb();	/* make sure PRD table writes are visible to controller */
	writel(ap->bmdma_prd_dma, base + RC_ATAPI_DTB_ADR);

	/* specify data direction, triple-check start bit is clear */
	hostctl = RC_ATAPI_CONTROL1_STANDARD;
	if (!rw)
		hostctl |= RC_ATAPI_CONTROL1_RW;
	writel(hostctl, base + RC_ATAPI_CONTROL1);

	/* issue r/w command */
	rcar_ata_sff_exec_command(ap, &qc->tf);
}

/**
 *	rcar_ata_bmdma_start - Start a IDE BMDMA transaction
 *	@qc: Info associated with this ATA transaction.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *base = rcar_ap_base(ap);
	u32 hostctl;

	/* start host DMA transaction */
	hostctl = readl(base + RC_ATAPI_CONTROL1) | RC_ATAPI_CONTROL1_START;
	writel(hostctl, base + RC_ATAPI_CONTROL1);
}

/**
 *	rcar_ata_bmdma_stop - Stop IDE BMDMA transfer
 *	@qc: Command we are ending DMA for
 *
 *	Clears the ATA_DMA_START flag in the dma control register
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static void rcar_ata_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	void __iomem *base = rcar_ap_base(ap);
	u32 irqstat;
	u32 hostctl;

	/* clear start/stop bit */
	irqstat = readl(base + RC_ATAPI_STATUS);
	if (irqstat & RC_ATAPI_STATUS_ACT) {
		hostctl = RC_ATAPI_CONTROL1_STANDARD | RC_ATAPI_CONTROL1_STOP;
		writel(hostctl, base + RC_ATAPI_CONTROL1);
	}

	/* one-PIO-cycle guaranteed wait, per spec, for HDMA1:0 transition */
	ata_sff_dma_pause(ap);
}

/**
 *	rcar_ata_bmdma_status - Read IDE BMDMA status
 *	@ap: Port associated with this ATA transaction.
 *
 *	Read and return BMDMA status register.
 *
 *	LOCKING:
 *	spin_lock_irqsave(host lock)
 */
static u8 rcar_ata_bmdma_status(struct ata_port *ap)
{
	void __iomem *base = rcar_ap_base(ap);
	struct rc_host_priv *hpriv = ap->host->private_data;
	u32 irqstat, status = 0;
	int retry = 0;

	do {
		irqstat = readl(base + RC_ATAPI_STATUS);
		if (irqstat & RC_ATAPI_STATUS_SSERR)
			status = ATA_DMA_INTR;		/* device error */
		else if (irqstat & RC_ATAPI_STATUS_DMAERR)
			status = ATA_DMA_ERR | ATA_DMA_INTR;	/* DMA error */
		else if (irqstat & RC_ATAPI_STATUS_DNEND || hpriv->incomplete)
			status = ATA_DMA_INTR;		/* DMA completion */
		else {
			if (retry++ < 25)
				ata_sff_pause(ap);
			else
				status = ATA_DMA_ERR | ATA_DMA_INTR;
		}
	} while (!status);

	return status;
}

static void rcar_phy_write(void __iomem *base, u32 addr, u32 rate, u32 data)
{
	int i;

	writel(1,    base + RC_SATAPHYACCEN);	/* lane enable */
	writel(data, base + RC_SATAPHYWDATA);	/* set data */
	addr |= (rate << 10) | (1 << 8);
	writel(addr, base + RC_SATAPHYADRR);	/* cmd & addr */

	for (i = 0; i < 100; i++) {
		if (readl(base + RC_SATAPHYACK) & 1)	/* get ack */
			break;
	}

	writel(0, base + RC_SATAPHYADRR);	/* clear cmd */
	writel(0, base + RC_SATAPHYACCEN);	/* clear lane enable */

	for (i = 0; i < 100; i++) {
		if (!(readl(base + RC_SATAPHYACK) & 1))	/* get ack down */
			break;
	}
}

static void rcar_init_phy(void __iomem *base)
{
	/* initialize PHY */
	rcar_phy_write(base, 0x43, 0, 0x00200188);
	rcar_phy_write(base, 0x43, 1, 0x00200188);
	rcar_phy_write(base, 0x5A, 0, 0x0000A061);
	rcar_phy_write(base, 0x52, 0, 0x20000000);
	rcar_phy_write(base, 0x52, 1, 0x20000000);
	rcar_phy_write(base, 0x60, 0, 0x28E80000);
}

static void rcar_init_port(struct ata_ioports *port, void __iomem *base)
{
	port->cmd_addr		=
	port->data_addr		= base + RC_SDATA;
	port->error_addr	= base + RC_SSSERR;
	port->feature_addr	= base + RC_SSFEATURES;
	port->nsect_addr	= base + RC_SSECCNT;
	port->lbal_addr		= base + RC_SLBALOW;
	port->lbam_addr		= base + RC_SLBAMID;
	port->lbah_addr		= base + RC_SLBAHIGH;
	port->device_addr	= base + RC_SDEVHEAD;
	port->status_addr	= base + RC_SSSTATUS;
	port->command_addr	= base + RC_SSCOM;
	port->altstatus_addr	= base + RC_SSALTSTS;
	port->ctl_addr		= base + RC_SSDEVCON;
	port->scr_addr		= base + RC_SCR_OFS;
}

static int rcar_init_host(struct ata_host *host)
{
	void __iomem *base = rcar_host_base(host);
	struct ata_port *ap = host->ports[0];
	u32 hostctl;

	/* reset host core */
	hostctl = RC_ATAPI_CONTROL1_RESET | RC_ATAPI_CONTROL1_ISM;
	writel(hostctl, base + RC_ATAPI_CONTROL1);
	hostctl = RC_ATAPI_CONTROL1_STANDARD;
	writel(hostctl, base + RC_ATAPI_CONTROL1);

	/* release PHY standby */
	rcar_init_phy(base);

	rcar_init_port(&ap->ioaddr, base);

	/* clear status */
	writel(0xFFFFFFFF, base + RC_SCR_OFS + (SCR_ERROR * 4));

	return 0;
}

static int rcar_platform_probe(struct platform_device *pdev)
{
	const struct ata_port_info *ppi[] = { &rc_port_info[0], NULL };
	struct ata_host *host;
	struct rc_host_priv *hpriv;
	struct resource *res;
	const int n_ports = 1;
	int ret;

	ata_print_version_once(&pdev->dev, DRV_VERSION);

	/*
	 * Get the register base first
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	/* allocate host */
	host = ata_host_alloc_pinfo(&pdev->dev, ppi, n_ports);
	hpriv = devm_kzalloc(&pdev->dev, sizeof(*hpriv), GFP_KERNEL);
	if (!host || !hpriv)
		return -ENOMEM;

	host->private_data = hpriv;

	host->iomap = NULL;
	hpriv->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));

#if defined(CONFIG_HAVE_CLK)
	hpriv->clk = clk_get(&pdev->dev, "sata");
	if (IS_ERR(hpriv->clk))
		dev_notice(&pdev->dev, "cannot get clkdev\n");
	else
		clk_enable(hpriv->clk);
#endif

	/* initialize adapter */
	ret = rcar_init_host(host);
	if (ret)
		goto err;

	return ata_host_activate(host, platform_get_irq(pdev, 0),
			ata_bmdma_interrupt, IRQF_SHARED, &rc_ata_sht);
err:
#if defined(CONFIG_HAVE_CLK)
	if (!IS_ERR(hpriv->clk)) {
		clk_disable(hpriv->clk);
		clk_put(hpriv->clk);
	}
#endif

	return ret;
}

static int rcar_platform_remove(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
#if defined(CONFIG_HAVE_CLK)
	struct rc_host_priv *hpriv = host->private_data;
#endif
	ata_host_detach(host);

#if defined(CONFIG_HAVE_CLK)
	if (!IS_ERR(hpriv->clk)) {
		clk_disable(hpriv->clk);
		clk_put(hpriv->clk);
	}
#endif

	return 0;
}

#ifdef CONFIG_PM
static int rcar_platform_suspend(struct platform_device *pdev,
		pm_message_t state)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	return ata_host_suspend(host, state);
}

static int rcar_platform_resume(struct platform_device *pdev)
{
	struct ata_host *host = dev_get_drvdata(&pdev->dev);
	int ret;

	/* initialize adapter */
	ret = rcar_init_host(host);
	if (ret) {
		dev_err(&pdev->dev, "Error initializing hardware\n");
		return ret;
	}
	ata_host_resume(host);

	return 0;
}
#endif

static struct platform_driver rc_sata_driver = {
	.probe			= rcar_platform_probe,
	.remove			= rcar_platform_remove,
#ifdef CONFIG_PM
	.suspend		= rcar_platform_suspend,
	.resume			= rcar_platform_resume,
#endif
	.driver			= {
				   .name = DRV_NAME,
				   .owner = THIS_MODULE,
				  },
};

module_platform_driver(rc_sata_driver);

MODULE_DESCRIPTION("R-Car Serial-ATA driver");
MODULE_LICENSE("GPL v2");
