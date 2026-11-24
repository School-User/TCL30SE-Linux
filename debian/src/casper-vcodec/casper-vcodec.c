// SPDX-License-Identifier: GPL-2.0
/*
 * casper-vcodec: /dev/Vcodec for the MT6765 hardware video decoder on a mainline kernel.
 *
 * Lean port of MediaTek's GPL "videocodec_kernel_driver" (drivers/misc/mediatek/videocodec/mt6765, as published
 * in the ZTE MT6761 kernel release). Same ioctl ABI (magic 'M') and struct layouts as the vendor userspace
 * (libvcodec_utility / libvcodecdrv) expects, incl. 32-bit callers. The vendor library programs the decoder
 * registers itself through mmap(); the kernel provides power, a hardware lock, and "frame done" interrupts.
 * Dropped from the original: DVFS/PM-QoS/EMI bandwidth, M4U, SMI debug, encoder IRQ (decoder only for now).
 * Power: SMI common + vcodecsys clocks enabled directly (runtime-PM of SMI larb1 oopses in mainline's
 * mtk_smi_larb_resume when no IOMMU is bound). The VCODEC power domain stays on (pd_ignore_unused).
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <dt-bindings/clock/mt6765-clk.h>

#define MFV_IOC_MAGIC 'M'
#define VCODEC_WAITISR			_IOW(MFV_IOC_MAGIC, 0x0b, unsigned int)
#define VCODEC_LOCKHW			_IOW(MFV_IOC_MAGIC, 0x0d, unsigned int)
#define VCODEC_PMEM_FLUSH		_IOW(MFV_IOC_MAGIC, 0x10, unsigned int)
#define VCODEC_PMEM_CLEAN		_IOW(MFV_IOC_MAGIC, 0x11, unsigned int)
#define VCODEC_INC_SYSRAM_USER		_IOW(MFV_IOC_MAGIC, 0x13, unsigned int)
#define VCODEC_DEC_SYSRAM_USER		_IOW(MFV_IOC_MAGIC, 0x14, unsigned int)
#define VCODEC_INC_ENC_EMI_USER		_IOW(MFV_IOC_MAGIC, 0x15, unsigned int)
#define VCODEC_DEC_ENC_EMI_USER		_IOW(MFV_IOC_MAGIC, 0x16, unsigned int)
#define VCODEC_INC_DEC_EMI_USER		_IOW(MFV_IOC_MAGIC, 0x17, unsigned int)
#define VCODEC_DEC_DEC_EMI_USER		_IOW(MFV_IOC_MAGIC, 0x18, unsigned int)
#define VCODEC_INITHWLOCK		_IOW(MFV_IOC_MAGIC, 0x20, unsigned int)
#define VCODEC_DEINITHWLOCK		_IOW(MFV_IOC_MAGIC, 0x21, unsigned int)
#define VCODEC_ALLOC_NON_CACHE_BUFFER	_IOW(MFV_IOC_MAGIC, 0x22, unsigned int)
#define VCODEC_FREE_NON_CACHE_BUFFER	_IOW(MFV_IOC_MAGIC, 0x23, unsigned int)
#define VCODEC_SET_THREAD_ID		_IOW(MFV_IOC_MAGIC, 0x24, unsigned int)
#define VCODEC_SET_SYSRAM_INFO		_IOW(MFV_IOC_MAGIC, 0x25, unsigned int)
#define VCODEC_GET_SYSRAM_INFO		_IOW(MFV_IOC_MAGIC, 0x26, unsigned int)
#define VCODEC_INC_PWR_USER		_IOW(MFV_IOC_MAGIC, 0x27, unsigned int)
#define VCODEC_DEC_PWR_USER		_IOW(MFV_IOC_MAGIC, 0x28, unsigned int)
#define VCODEC_GET_CPU_LOADING_INFO	_IOW(MFV_IOC_MAGIC, 0x29, unsigned int)
#define VCODEC_GET_CORE_LOADING		_IOW(MFV_IOC_MAGIC, 0x30, unsigned int)
#define VCODEC_GET_CORE_NUMBER		_IOW(MFV_IOC_MAGIC, 0x31, unsigned int)
#define VCODEC_SET_CPU_OPP_LIMIT	_IOW(MFV_IOC_MAGIC, 0x32, unsigned int)
#define VCODEC_UNLOCKHW			_IOW(MFV_IOC_MAGIC, 0x33, unsigned int)
#define VCODEC_MB			_IOW(MFV_IOC_MAGIC, 0x34, unsigned int)
#define VCODEC_SET_LOG_COUNT		_IOW(MFV_IOC_MAGIC, 0x35, unsigned int)
#define VCODEC_SET_AV_TASK_GROUP	_IOW(MFV_IOC_MAGIC, 0x36, unsigned int)
#define VCODEC_SET_FRAME_INFO		_IOW(MFV_IOC_MAGIC, 0x37, unsigned int)

/* VAL_DRIVER_TYPE_T values (val_types_public.h) */
enum { T_MP4_ENC = 1, T_H263_ENC = 3, T_H264_ENC = 5, T_VP8_ENC = 24, T_JPEG_ENC = 27, T_HEVC_ENC = 28 };
static bool is_encoder(u32 t) { return t == T_MP4_ENC || t == T_H263_ENC || t == T_H264_ENC || t == T_VP8_ENC || t == T_JPEG_ENC || t == T_HEVC_ENC; }

/* Native (64-bit) layouts, as in val_types_public.h. */
struct val_hw_lock { u64 pvHandle; u32 u4HandleSize; u64 pvLock; u32 u4TimeoutMs; u64 pvReserved; u32 u4ReservedSize; u32 eDriverType; s8 bSecureInst; };
struct val_isr { u64 pvHandle; u32 u4HandleSize; u32 eDriverType; u64 pvIsrFunction; u64 pvReserved; u32 u4ReservedSize; u32 u4TimeoutMs; u32 u4IrqStatusNum; u32 u4IrqStatus[16]; };
struct val_power { u64 pvHandle; u32 u4HandleSize; u32 eDriverType; };
/* 32-bit (compat) layouts */
struct val_hw_lock32 { u32 pvHandle; u32 u4HandleSize; u32 pvLock; u32 u4TimeoutMs; u32 pvReserved; u32 u4ReservedSize; u32 eDriverType; s8 bSecureInst; };
struct val_isr32 { u32 pvHandle; u32 u4HandleSize; u32 eDriverType; u32 pvIsrFunction; u32 pvReserved; u32 u4ReservedSize; u32 u4TimeoutMs; u32 u4IrqStatusNum; u32 u4IrqStatus[16]; };
struct val_power32 { u32 pvHandle; u32 u4HandleSize; u32 eDriverType; };

/* Parsed request (what the logic needs) */
struct req { u64 handle; u32 type; u32 timeout_ms; };

#define VCODEC_PHYS	0x17000000UL	/* vcodecsys: gcon 0x17000000, larb1 +0x10000, venc +0x20000, jpgenc +0x30000, vdec +0x40000 */
#define VCODEC_SIZE	0x50000UL
#define VDEC_OFF	0x40000UL
#define VDEC_IRQ_SPI	233		/* stock DT: vdec interrupts = <0 0xe9 IRQ_TYPE_LEVEL_LOW> */

static void __iomem *vbase;		/* whole vcodec window */
#define VDEC_VLD	(vbase + VDEC_OFF)
#define VDEC_MISC	(vbase + VDEC_OFF + 0x5000)

/* mmsys SMI common clocks, then vcodecsys larb1/venc(=larb1 smi)/vdec gates. Order = enable order. */
static struct { const char *prov; int id; const char *name; struct clk *clk; } clks[] = {
	{ "mediatek,mt6765-mmsys", CLK_MM_SMI_COMMON, "smi-common" },
	{ "mediatek,mt6765-mmsys", CLK_MM_SMI_COMM0, "smi-comm0" },
	{ "mediatek,mt6765-mmsys", CLK_MM_SMI_COMM1, "smi-comm1" },
	{ "mediatek,mt6765-vcodecsys", CLK_VENC_SET0_LARB, "venc-larb" },
	{ "mediatek,mt6765-vcodecsys", CLK_VENC_SET1_VENC, "venc" },
	{ "mediatek,mt6765-vcodecsys", CLK_VENC_SET3_VDEC, "vdec" },
};
static int vdec_irq = -1;

static DEFINE_MUTEX(pwr_lock);
static int pwr_users;

#define LARB1_OFF		0x10000
#define LARB1_NR_PORTS		11
#define LARB_NONSEC_CON(p)	(0x380 + (p) * 4)
#define GCON_VDEC_SEL		0x10
#define GCON_VCODEC_SEL		0x20	/* "cervino VCODEC_SEL": 0 idle, 1 venc, 2 vdec, 4 jpgenc */
#define VCODEC_SEL_VDEC		2

static DEFINE_MUTEX(hw_lock);
static DECLARE_WAIT_QUEUE_HEAD(hw_wq);
static struct { u64 handle; pid_t tgid; u32 type; struct file *filp; } owner;

static DECLARE_WAIT_QUEUE_HEAD(isr_wq);
static atomic_t dec_done = ATOMIC_INIT(0);
static atomic_t dec_irqs = ATOMIC_INIT(0);

static int power_get(void)
{
	int ret = 0, i;

	mutex_lock(&pwr_lock);
	if (pwr_users == 0) {
		for (i = 0; i < ARRAY_SIZE(clks); i++) {
			ret = clk_prepare_enable(clks[i].clk);
			if (ret) {
				pr_err("casper-vcodec: clock %s: %d\n", clks[i].name, ret);
				while (--i >= 0)
					clk_disable_unprepare(clks[i].clk);
				goto out;
			}
		}
		/* larb1 is gen2: per-port NONSEC_CON bit0 = go through the IOMMU. The bootloader leaves all 11 set;
		 * there is no IOMMU here, so every port must use physical addresses. (mainline's larb config_port
		 * would do this but oopses without an IOMMU.) */
		for (i = 0; i < LARB1_NR_PORTS; i++)
			writel(readl(vbase + LARB1_OFF + LARB_NONSEC_CON(i)) & ~1u, vbase + LARB1_OFF + LARB_NONSEC_CON(i));
		/* shared vdec/venc core: hand the memory ports to the decoder (vendor does this in LOCKHW) */
		writel(VCODEC_SEL_VDEC, vbase + GCON_VCODEC_SEL);
		writel(1, vbase + GCON_VDEC_SEL);
		enable_irq(vdec_irq);	/* only while the block is powered: the ISR reads VDEC registers */
	}
	pwr_users++;
out:
	mutex_unlock(&pwr_lock);
	return ret;
}

static void power_put(void)
{
	int i;

	mutex_lock(&pwr_lock);
	if (pwr_users > 0 && --pwr_users == 0) {
		disable_irq(vdec_irq);
		writel(0, vbase + GCON_VCODEC_SEL);
		for (i = ARRAY_SIZE(clks) - 1; i >= 0; i--)
			clk_disable_unprepare(clks[i].clk);
	}
	mutex_unlock(&pwr_lock);
}

static irqreturn_t vdec_isr(int irq, void *data)
{
	u32 st = readl(VDEC_MISC + 0xA4);

	atomic_inc(&dec_irqs);
	if (!(st & BIT(16)))
		return IRQ_HANDLED;		/* same as the original: not a "decode done" interrupt */
	/* clear: MISC_41 |= 0x11, then &= ~0x10 */
	writel(readl(VDEC_MISC + 41 * 4) | 0x11, VDEC_MISC + 41 * 4);
	writel(readl(VDEC_MISC + 41 * 4) & ~0x10, VDEC_MISC + 41 * 4);
	atomic_set(&dec_done, 1);
	wake_up(&isr_wq);
	return IRQ_HANDLED;
}

static bool is_owner(struct req *r)
{
	return owner.handle == r->handle && owner.tgid == current->tgid;
}

static long do_lock(struct file *filp, struct req *r)
{
	long ret;

	if (is_encoder(r->type))
		return -EINVAL;			/* decoder only in this port */
	for (;;) {
		mutex_lock(&hw_lock);
		if (owner.handle == 0 || is_owner(r)) {
			bool fresh = owner.handle == 0;

			owner.handle = r->handle; owner.tgid = current->tgid; owner.type = r->type; owner.filp = filp;
			mutex_unlock(&hw_lock);
			if (fresh) {
				ret = power_get();
				if (ret) {
					mutex_lock(&hw_lock); owner.handle = 0; mutex_unlock(&hw_lock);
					wake_up(&hw_wq);
					return ret;
				}
				atomic_set(&dec_done, 0);
			}
			return 0;
		}
		mutex_unlock(&hw_lock);
		/* the original retries in 1 s steps until it gets the lock */
		ret = wait_event_interruptible_timeout(hw_wq, READ_ONCE(owner.handle) == 0, HZ);
		if (ret < 0)
			return -ERESTARTSYS;
	}
}

static void release_owner_locked(void)
{
	owner.handle = 0; owner.tgid = 0; owner.filp = NULL;
}

static long do_unlock(struct req *r)
{
	mutex_lock(&hw_lock);
	if (!is_owner(r)) {
		mutex_unlock(&hw_lock);
		pr_info("casper-vcodec: UNLOCKHW by non-owner (type %u)\n", r->type);
		return -EFAULT;
	}
	release_owner_locked();
	mutex_unlock(&hw_lock);
	power_put();
	wake_up(&hw_wq);
	return 0;
}

static long do_waitisr(struct req *r)
{
	long t;

	if (is_encoder(r->type))
		return -EINVAL;
	mutex_lock(&hw_lock);
	if (!is_owner(r)) {
		mutex_unlock(&hw_lock);
		pr_info("casper-vcodec: WAITISR without the HW lock\n");
		return -EFAULT;
	}
	mutex_unlock(&hw_lock);
	t = wait_event_interruptible_timeout(isr_wq, atomic_read(&dec_done),
					     msecs_to_jiffies(r->timeout_ms ? r->timeout_ms : 1000));
	if (t < 0)
		return -ERESTARTSYS;
	if (t == 0)
		return -2;			/* original: VAL_RESULT_INVALID_ISR (timeout) */
	atomic_set(&dec_done, 0);
	return 0;
}

/* ---- argument parsing for native and 32-bit callers ---- */
static int get_lock_req(unsigned long arg, bool compat, struct req *r)
{
	if (compat) {
		struct val_hw_lock32 l;

		if (copy_from_user(&l, compat_ptr(arg), sizeof(l)))
			return -EFAULT;
		r->handle = l.pvHandle; r->type = l.eDriverType; r->timeout_ms = l.u4TimeoutMs;
	} else {
		struct val_hw_lock l;

		if (copy_from_user(&l, (void __user *)arg, sizeof(l)))
			return -EFAULT;
		r->handle = l.pvHandle; r->type = l.eDriverType; r->timeout_ms = l.u4TimeoutMs;
	}
	return 0;
}

static int get_isr_req(unsigned long arg, bool compat, struct req *r)
{
	if (compat) {
		struct val_isr32 s;

		if (copy_from_user(&s, compat_ptr(arg), sizeof(s)))
			return -EFAULT;
		r->handle = s.pvHandle; r->type = s.eDriverType; r->timeout_ms = s.u4TimeoutMs;
	} else {
		struct val_isr s;

		if (copy_from_user(&s, (void __user *)arg, sizeof(s)))
			return -EFAULT;
		r->handle = s.pvHandle; r->type = s.eDriverType; r->timeout_ms = s.u4TimeoutMs;
	}
	return 0;
}

static long vcodec_ioctl_common(struct file *filp, unsigned int cmd, unsigned long arg, bool compat)
{
	struct req r = {};
	int ncpu;
	int ret;

	switch (cmd) {
	case VCODEC_LOCKHW:
		ret = get_lock_req(arg, compat, &r);
		return ret ? ret : do_lock(filp, &r);
	case VCODEC_UNLOCKHW:
		ret = get_lock_req(arg, compat, &r);
		return ret ? ret : do_unlock(&r);
	case VCODEC_WAITISR:
		ret = get_isr_req(arg, compat, &r);
		return ret ? ret : do_waitisr(&r);
	case VCODEC_GET_CORE_NUMBER:
		ncpu = nr_cpu_ids;
		return copy_to_user(compat ? compat_ptr(arg) : (void __user *)arg, &ncpu, sizeof(int)) ? -EFAULT : 0;
	case VCODEC_MB:
		mb();
		return 0;
	/* empty in the original MT6765 driver too (or bandwidth/DVFS hints we do not need) */
	case VCODEC_INC_PWR_USER: case VCODEC_DEC_PWR_USER:
	case VCODEC_INITHWLOCK: case VCODEC_DEINITHWLOCK:
	case VCODEC_SET_THREAD_ID: case VCODEC_ALLOC_NON_CACHE_BUFFER: case VCODEC_FREE_NON_CACHE_BUFFER:
	case VCODEC_INC_DEC_EMI_USER: case VCODEC_DEC_DEC_EMI_USER: case VCODEC_INC_ENC_EMI_USER: case VCODEC_DEC_ENC_EMI_USER:
	case VCODEC_GET_CPU_LOADING_INFO: case VCODEC_GET_CORE_LOADING: case VCODEC_SET_CPU_OPP_LIMIT:
	case VCODEC_SET_LOG_COUNT: case VCODEC_SET_AV_TASK_GROUP: case VCODEC_SET_FRAME_INFO:
	case VCODEC_PMEM_FLUSH: case VCODEC_PMEM_CLEAN: case VCODEC_INC_SYSRAM_USER: case VCODEC_DEC_SYSRAM_USER:
	case VCODEC_SET_SYSRAM_INFO: case VCODEC_GET_SYSRAM_INFO:
		return 0;
	default:
		pr_info("casper-vcodec: unknown ioctl 0x%x\n", cmd);
		return -EINVAL;
	}
}

static long vcodec_ioctl(struct file *f, unsigned int cmd, unsigned long arg) { return vcodec_ioctl_common(f, cmd, arg, false); }
#ifdef CONFIG_COMPAT
static long vcodec_compat_ioctl(struct file *f, unsigned int cmd, unsigned long arg) { return vcodec_ioctl_common(f, cmd, arg, true); }
#endif

/* mmap: only the register windows the original driver allows (physical address in the page offset). */
static int vcodec_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	unsigned long pa = vma->vm_pgoff << PAGE_SHIFT;
	bool ok = (pa >= VCODEC_PHYS && pa + len <= VCODEC_PHYS + VCODEC_SIZE) ||	/* vcodecsys incl. vdec/venc */
		  (pa >= 0x10000000UL && pa + len <= 0x10001000UL) ||			/* INFO (topckgen chip id) */
		  (pa >= 0x07FFF000UL && pa + len <= 0x08001000UL);			/* HW_BASE (as in the original) */

	if (!ok) {
		pr_info("casper-vcodec: mmap refused pa=0x%lx len=0x%lx\n", pa, len);
		return -EINVAL;
	}
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, len, vma->vm_page_prot) ? -EAGAIN : 0;
}

static int vcodec_open(struct inode *inode, struct file *file) { return 0; }

static int vcodec_release(struct inode *inode, struct file *file)
{
	bool drop = false;

	mutex_lock(&hw_lock);
	if (owner.handle && (owner.filp == file || owner.tgid == current->tgid)) {
		pr_info("casper-vcodec: releasing HW lock held by exiting process %d\n", owner.tgid);
		release_owner_locked();
		drop = true;
	}
	mutex_unlock(&hw_lock);
	if (drop) {
		power_put();
		wake_up(&hw_wq);
	}
	return 0;
}

static const struct file_operations vcodec_fops = {
	.owner = THIS_MODULE,
	.open = vcodec_open,
	.release = vcodec_release,
	.unlocked_ioctl = vcodec_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = vcodec_compat_ioctl,
#endif
	.mmap = vcodec_mmap,
};

static struct miscdevice vcodec_misc = { .minor = MISC_DYNAMIC_MINOR, .name = "Vcodec", .fops = &vcodec_fops, .mode = 0666 };

/* ---- debug: /sys/module/casper_vcodec/parameters/probe_regs: power on, read a few IDs, power off ---- */
static int dbg_get(char *buf, const struct kernel_param *kp)
{
	int ret = power_get(), n;

	if (ret)
		return sysfs_emit(buf, "power_get failed %d\n", ret);
	n = sysfs_emit(buf, "larb1 nonsec0=%08x vcodec_sel=%08x gcon[0]=%08x gcon[4]=%08x vld[61]=%08x misc[0xA4]=%08x misc[41]=%08x irqs=%d\n",
		       readl(vbase + LARB1_OFF + LARB_NONSEC_CON(0)), readl(vbase + GCON_VCODEC_SEL), readl(vbase), readl(vbase + 4), readl(VDEC_VLD + 61 * 4), readl(VDEC_MISC + 0xA4),
		       readl(VDEC_MISC + 41 * 4), atomic_read(&dec_irqs));
	power_put();
	return n;
}
static const struct kernel_param_ops dbg_ops = { .get = dbg_get };
module_param_cb(probe_regs, &dbg_ops, NULL, 0444);

/* debug: echo "OFF [VAL]" > regs  (offsets inside the 0x17000000 window; power is held for the access),
 * cat regs -> last read. Dumps 16 words from OFF when no VAL is given. */
static char reg_out[256];
static int reg_set(const char *val, const struct kernel_param *kp)
{
	unsigned int off, v, i; int n = sscanf(val, "%x %x", &off, &v), ret;

	if (n < 1 || off > VCODEC_SIZE - 64 || (off & 3))
		return -EINVAL;
	ret = power_get();
	if (ret)
		return ret;
	if (n == 2)
		writel(v, vbase + off);
	for (i = 0, n = 0; i < 16; i++)
		n += scnprintf(reg_out + n, sizeof(reg_out) - n, "%08x%c", readl(vbase + off + i * 4), i == 15 ? '\n' : ' ');
	power_put();
	return 0;
}
static int reg_get(char *buf, const struct kernel_param *kp) { return sysfs_emit(buf, "%s", reg_out); }
static const struct kernel_param_ops reg_ops = { .set = reg_set, .get = reg_get };
module_param_cb(regs, &reg_ops, NULL, 0644);

static int map_vdec_irq(void)
{
	struct device_node *np = of_find_compatible_node(NULL, NULL, "mediatek,mt6577-sysirq");
	struct irq_fwspec fw = { .param_count = 3, .param = { 0 /* GIC_SPI */, VDEC_IRQ_SPI, IRQ_TYPE_LEVEL_LOW } };
	int irq;

	if (!np)
		return -ENODEV;
	fw.fwnode = of_fwnode_handle(np);
	irq = irq_create_fwspec_mapping(&fw);
	of_node_put(np);
	return irq > 0 ? irq : -EINVAL;
}

static void put_clks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		if (!IS_ERR_OR_NULL(clks[i].clk))
			clk_put(clks[i].clk);
}

static int __init casper_vcodec_init(void)
{
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		struct of_phandle_args a = { .args_count = 1, .args = { clks[i].id } };

		a.np = of_find_compatible_node(NULL, NULL, clks[i].prov);
		clks[i].clk = a.np ? of_clk_get_from_provider(&a) : ERR_PTR(-ENODEV);
		of_node_put(a.np);
		if (IS_ERR(clks[i].clk)) {
			ret = PTR_ERR(clks[i].clk);
			pr_err("casper-vcodec: clock %s: %d\n", clks[i].name, ret);
			goto put;
		}
	}
	vbase = ioremap(VCODEC_PHYS, VCODEC_SIZE);
	if (!vbase) {
		ret = -ENOMEM;
		goto put;
	}
	vdec_irq = map_vdec_irq();
	if (vdec_irq < 0) {
		ret = vdec_irq;
		pr_err("casper-vcodec: cannot map VDEC irq: %d\n", ret);
		goto unmap;
	}
	ret = request_irq(vdec_irq, vdec_isr, IRQF_NO_AUTOEN, "casper-vcodec-vdec", NULL);
	if (ret)
		goto unmap;
	ret = misc_register(&vcodec_misc);
	if (ret)
		goto irq;
	pr_info("casper-vcodec: /dev/Vcodec ready (vdec irq %d = SPI %d)\n", vdec_irq, VDEC_IRQ_SPI);
	return 0;
irq:
	free_irq(vdec_irq, NULL);
unmap:
	iounmap(vbase);
put:
	put_clks();
	return ret;
}

static void __exit casper_vcodec_exit(void)
{
	misc_deregister(&vcodec_misc);
	free_irq(vdec_irq, NULL);
	while (pwr_users > 0)
		power_put();
	iounmap(vbase);
	put_clks();
}

module_init(casper_vcodec_init);
module_exit(casper_vcodec_exit);
MODULE_DESCRIPTION("MT6765 /dev/Vcodec (hardware video decoder) for mainline; port of MediaTek's GPL videocodec driver");
MODULE_LICENSE("GPL");
