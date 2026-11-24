// SPDX-License-Identifier: GPL-2.0
/*
 * Manual CPU DVFS for the MT6765/MT6762 (TCL 30 SE): raise/lower the two CPU PLLs and the shared VPROC rail from debugfs.
 * Sequence copied from MediaTek's vendor cpufreq (mach/mt6765/mtk_cpufreq_platform.c): move the cluster mux to the
 * 26 MHz source, retune the ARM PLL, wait, mux back; voltage up BEFORE frequency up, down AFTER frequency down.
 * Nothing happens at boot: this only acts on writes to /sys/kernel/debug/casper-dvfs/{big,little}_khz.
 *   cl_l  = cpu0-3  = ARMPLL_L, CKDIV1_L_CFG  (MCUCFG+0x7a0)   boots at ~1.5 GHz
 *   cl_ll = cpu4-7  = ARMPLL,   CKDIV1_LL_CFG (MCUCFG+0x7a4)   boots at ~0.92 GHz
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#define CKDIV_L		0x7a0
#define CKDIV_LL	0x7a4
#define MUX_SHIFT	9
#define MUX_MASK	(3u << MUX_SHIFT)
#define MUX_CLKSQ	0
#define MUX_ARMPLL	1

/* stock Android OPP voltages (mV), from the stock DT cluster tables: {kHz, mV} */
struct opp { unsigned int khz, mv; };
static const struct opp l_opp[] = { {900000,600},{979000,618},{1085000,643},{1218000,675},{1351000,706},{1484000,737},
	{1617000,768},{1814000,837},{1879000,875},{1944000,912},{2009000,950},{2074000,987},{2139000,1025},{2215000,1068},{2301000,1118} };
static const struct opp ll_opp[] = { {400000,600},{501000,631},{542000,643},{643000,675},{745000,706},{846000,737},
	{948000,768},{1138000,837},{1226000,875},{1314000,912},{1402000,950},{1491000,987},{1579000,1025},{1682000,1068},{1800000,1118} };
#define MIN_MV		800	/* never go below what the bootloader left: both clusters currently run on it */

struct dvfs {
	void __iomem *mcucfg;
	struct clk *pll[2];		/* 0 = L (cpu0-3), 1 = LL (cpu4-7) */
	struct regulator *vproc, *vsram;
	unsigned int khz[2];
	struct mutex lock;
};
static struct dvfs *D;

static unsigned int need_mv(const struct opp *t, int n, unsigned int khz)
{
	for (int i = 0; i < n; i++)
		if (khz <= t[i].khz) return max_t(unsigned int, t[i].mv, MIN_MV);
	return t[n - 1].mv;
}

static unsigned int rail_mv(void)
{
	return max(need_mv(l_opp, ARRAY_SIZE(l_opp), D->khz[0]), need_mv(ll_opp, ARRAY_SIZE(ll_opp), D->khz[1]));
}

static int set_rail(unsigned int mv)
{
	unsigned int sram = max(900u, mv + 100);	/* bootloader leaves 800/900: keep SRAM >= 100 mV above the core rail */
	int cur = regulator_get_voltage(D->vproc) / 1000, ret;

	if ((int)mv > cur) {		/* rising: SRAM first */
		ret = regulator_set_voltage(D->vsram, sram * 1000, (sram + 12) * 1000);
		if (!ret) ret = regulator_set_voltage(D->vproc, mv * 1000, (mv + 12) * 1000);
	} else {
		ret = regulator_set_voltage(D->vproc, mv * 1000, (mv + 12) * 1000);
		if (!ret) ret = regulator_set_voltage(D->vsram, sram * 1000, (sram + 12) * 1000);
	}
	udelay(200);
	pr_info("casper-dvfs: rail request vproc %u mV / vsram %u mV -> ret %d, readback vproc %d mV, vsram %d mV\n", mv, sram, ret,
		regulator_get_voltage(D->vproc) / 1000, regulator_get_voltage(D->vsram) / 1000);
	return ret;
}

static int set_cluster(int cl, unsigned int khz)
{
	void __iomem *div = D->mcucfg + (cl ? CKDIV_LL : CKDIV_L);
	const struct opp *t = cl ? ll_opp : l_opp;
	unsigned int old = D->khz[cl], mv_before, mv_after;
	u32 v;
	int ret = 0;

	if (khz < t[0].khz || khz > t[14].khz) return -EINVAL;
	D->khz[cl] = khz; mv_after = rail_mv(); D->khz[cl] = old; mv_before = rail_mv();

	if (khz > old) { ret = set_rail(max(mv_before, mv_after)); if (ret) return ret; }

	/* the cluster runs from this PLL: park it on the 26 MHz source while the PLL retunes */
	v = readl(div); writel((v & ~MUX_MASK) | (MUX_CLKSQ << MUX_SHIFT), div);
	ret = clk_set_rate(D->pll[cl], (unsigned long)khz * 1000);
	udelay(50);
	v = readl(div); writel((v & ~MUX_MASK) | (MUX_ARMPLL << MUX_SHIFT), div);
	if (ret) { pr_err("casper-dvfs: clk_set_rate(%u kHz) failed: %d (cluster left on its PLL)\n", khz, ret); return ret; }
	D->khz[cl] = khz;

	if (khz < old) ret = set_rail(mv_after);
	pr_info("casper-dvfs: cluster %s -> %lu kHz (pll reports %lu), rail %u mV\n", cl ? "LL cpu4-7" : "L cpu0-3", (unsigned long)khz,
		clk_get_rate(D->pll[cl]) / 1000, rail_mv());
	return ret;
}

static int set_get(void *data, u64 *val) { *val = D->khz[(long)data]; return 0; }
static int set_set(void *data, u64 val)
{
	int ret;
	mutex_lock(&D->lock);
	ret = set_cluster((long)data, (unsigned int)val);
	mutex_unlock(&D->lock);
	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops, set_get, set_set, "%llu\n");

static int casper_dvfs_probe(struct platform_device *pdev)
{
	struct dentry *dir;

	D = devm_kzalloc(&pdev->dev, sizeof(*D), GFP_KERNEL);
	if (!D) return -ENOMEM;
	mutex_init(&D->lock);
	D->mcucfg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(D->mcucfg)) return PTR_ERR(D->mcucfg);
	D->pll[0] = devm_clk_get(&pdev->dev, "pll_l");
	D->pll[1] = devm_clk_get(&pdev->dev, "pll_ll");
	if (IS_ERR(D->pll[0]) || IS_ERR(D->pll[1])) return dev_err_probe(&pdev->dev, -EPROBE_DEFER, "PLL clocks\n");
	D->vproc = devm_regulator_get(&pdev->dev, "vproc");
	D->vsram = devm_regulator_get(&pdev->dev, "vsram");
	if (IS_ERR(D->vproc) || IS_ERR(D->vsram)) return dev_err_probe(&pdev->dev, -EPROBE_DEFER, "regulators\n");
	D->khz[0] = clk_get_rate(D->pll[0]) / 1000;
	D->khz[1] = clk_get_rate(D->pll[1]) / 1000;
	dir = debugfs_create_dir("casper-dvfs", NULL);
	debugfs_create_file_unsafe("l_khz", 0644, dir, (void *)0L, &fops);	/* cpu0-3 */
	debugfs_create_file_unsafe("ll_khz", 0644, dir, (void *)1L, &fops);	/* cpu4-7 */
	dev_info(&pdev->dev, "ready (manual): L %u kHz, LL %u kHz, vproc %d mV, vsram %d mV; nothing changes until debugfs is written\n",
		 D->khz[0], D->khz[1], regulator_get_voltage(D->vproc) / 1000, regulator_get_voltage(D->vsram) / 1000);
	return 0;
}

static const struct of_device_id ids[] = { { .compatible = "casper,mt6765-dvfs" }, { } };
MODULE_DEVICE_TABLE(of, ids);
static struct platform_driver drv = { .probe = casper_dvfs_probe, .driver = { .name = "casper-mt6765-dvfs", .of_match_table = ids } };
module_platform_driver(drv);
MODULE_DESCRIPTION("manual CPU DVFS for MT6765");
MODULE_LICENSE("GPL");
