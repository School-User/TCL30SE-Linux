// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal MT6357 ACCDET headphone plug detection for the TCL 30 SE (cruze).
 * Init sequence from the vendor mt6357-accdet.c (EINT0, ACC mode 1, DT
 * headset-mode-setting = <0x500 0x500 0x01 0x1f0 ...>). Reports plug state as
 * SW_HEADPHONE_INSERT on an input device; userspace switches the mixer.
 *
 * ponytail: polls the EINT0 level (CON18 bit14) every 200 ms instead of wiring the PMIC
 * ACCDET_EINT0 irq; move to the irq (mt6357 mfd cell) if latency or wakeups matter.
 * No mic/button detection.
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

#define AUD_TOP_CKPDN_CON0	0x208c	/* bit0 accdet clock power-down */
#define AUD_TOP_RST_CON0	0x209a	/* bit1 accdet reset */
#define AUDENC_ANA_CON6		0x2194	/* audspare */
#define AUDENC_ANA_CON9		0x219a	/* micbias1 vref [6:4] */
#define AUDENC_ANA_CON10	0x219c	/* accdet mode */
#define ACCDET_CON1		0x230a	/* bit1 seq_init, bit2 eint0_en */
#define ACCDET_CON2		0x230c	/* bit3 eint0 pwm_en, bit11 eint0 pwm_idle */
#define ACCDET_CON3		0x230e	/* pwm width */
#define ACCDET_CON4		0x2310	/* pwm thresh */
#define ACCDET_CON5		0x2312	/* fall << 15 | rise delay */
#define ACCDET_CON12		0x2320	/* bit2 eint0 irq, bit10 clr, bit14 polarity */
#define ACCDET_CON15		0x2326	/* eint0 debounce [6:3], pwm thresh [10:8], width [13:12] */
#define ACCDET_CON18		0x232c
#define ACCDET_CON20		0x2330
#define ACCDET_CON24		0x2338	/* hwen_sel */

#define EINT0_IRQ	BIT(2)
#define EINT0_CLR	BIT(10)
#define EINT0_POL	BIT(14)

static bool debug;
module_param(debug, bool, 0644);

static struct device *pdev_ref;
static struct regmap *map;
static struct input_dev *idev;
static struct delayed_work poll;
static bool plugged;

static u32 rd(u32 r)
{
	unsigned int v = 0;

	regmap_read(map, r, &v);
	return v;
}

static void set_polarity(void)
{
	/* vendor, IRQ_TYPE_LEVEL_LOW: plugged out -> polarity 1, plugged in -> 0 */
	regmap_update_bits(map, ACCDET_CON12, EINT0_POL, plugged ? 0 : EINT0_POL);
}

static void clear_irq(void)
{
	regmap_update_bits(map, ACCDET_CON12, EINT0_CLR, EINT0_CLR);
	udelay(200);
	regmap_update_bits(map, ACCDET_CON12, EINT0_CLR, 0);
}

static void poll_fn(struct work_struct *w)
{
	static u32 last12 = ~0, last18 = ~0, last20 = ~0;
	u32 c12 = rd(ACCDET_CON12), c18 = rd(ACCDET_CON18), c20 = rd(ACCDET_CON20);

	if (debug && (c12 != last12 || c18 != last18 || c20 != last20))
		pr_info("mt6357-jack: CON12=%04x CON18=%04x CON20=%04x plugged=%d\n",
			c12, c18, c20, plugged);
	last12 = c12; last18 = c18; last20 = c20;

	/*
	 * Measured 2026-11-11: CON18 bit14 is the debounced EINT0 level, 1 = unplugged,
	 * 0 = plugged (it followed in/out/in exactly; the irq latch missed the first plug).
	 * Report after two equal reads in a row.
	 */
	{
		static int pending = -1;
		bool level = !(c18 & BIT(14));

		if (c12 & EINT0_IRQ)
			clear_irq();
		if (level != plugged && level == pending) {
			plugged = level;
			pr_info("mt6357-jack: headphones %s\n", plugged ? "IN" : "OUT");
			input_report_switch(idev, SW_HEADPHONE_INSERT, plugged);
			input_sync(idev);
		}
		pending = level;
	}
	schedule_delayed_work(&poll, msecs_to_jiffies(200));
}

static void accdet_hw_init(void)
{
	regmap_update_bits(map, AUD_TOP_CKPDN_CON0, BIT(0), 0);
	regmap_update_bits(map, AUD_TOP_RST_CON0, BIT(1), BIT(1));
	regmap_update_bits(map, AUD_TOP_RST_CON0, BIT(1), 0);

	regmap_write(map, ACCDET_CON3, 0x500 - 1);
	regmap_write(map, ACCDET_CON4, 0x500 - 1);
	regmap_write(map, ACCDET_CON5, 0x01 << 15 | 0x1f0);

	regmap_update_bits(map, AUDENC_ANA_CON9, 0x7 << 4, 6 << 4);	/* micbias1 vref */
	regmap_update_bits(map, AUDENC_ANA_CON10, 0x0807, 0x0807);	/* ACC mode 1 */
	regmap_update_bits(map, AUDENC_ANA_CON6, BIT(7) | BIT(5), BIT(7) | BIT(5));

	regmap_update_bits(map, ACCDET_CON15, 0x6 << 8, 0x6 << 8);	/* eint0 pwm thresh */
	regmap_update_bits(map, ACCDET_CON15, 0x2 << 12, 0x2 << 12);	/* eint0 pwm width */
	regmap_update_bits(map, ACCDET_CON2, BIT(3) | BIT(11), BIT(3) | BIT(11));
	regmap_update_bits(map, ACCDET_CON1, BIT(2), BIT(2));		/* eint0 enable */

	regmap_write(map, ACCDET_CON24, (rd(ACCDET_CON24) & ~BIT(2)) | BIT(4));	/* sw mode */

	regmap_update_bits(map, ACCDET_CON1, BIT(1), BIT(1));		/* seq init */
	usleep_range(2000, 3000);
	regmap_update_bits(map, ACCDET_CON1, BIT(1), 0);
	usleep_range(1000, 1500);
	regmap_update_bits(map, ACCDET_CON15, 0xf << 3, 0x0e << 3);	/* plug-in debounce */

	plugged = false;
	set_polarity();
	clear_irq();
}

static int __init mt6357_jack_init(void)
{
	struct device *dev;
	int ret;

	dev = bus_find_device_by_name(&platform_bus_type, NULL, "mt6357-sound");
	if (!dev)
		return -EPROBE_DEFER;
	/* mt6357-sound -> mt6357 pmic -> pwrap, which owns the regmap */
	map = (dev->parent && dev->parent->parent) ?
		dev_get_regmap(dev->parent->parent, NULL) : NULL;
	if (!map) {
		put_device(dev);
		return -ENODEV;
	}
	pdev_ref = dev;

	idev = input_allocate_device();
	if (!idev) {
		put_device(dev);
		return -ENOMEM;
	}
	idev->name = "Headphone Jack";
	idev->phys = "mt6357-accdet/input0";
	input_set_capability(idev, EV_SW, SW_HEADPHONE_INSERT);
	ret = input_register_device(idev);
	if (ret) {
		input_free_device(idev);
		put_device(dev);
		return ret;
	}

	accdet_hw_init();
	pr_info("mt6357-jack: init done CON1=%04x CON2=%04x CON12=%04x CON15=%04x CON24=%04x\n",
		rd(ACCDET_CON1), rd(ACCDET_CON2), rd(ACCDET_CON12), rd(ACCDET_CON15), rd(ACCDET_CON24));

	INIT_DELAYED_WORK(&poll, poll_fn);
	schedule_delayed_work(&poll, msecs_to_jiffies(200));
	return 0;
}

static void __exit mt6357_jack_exit(void)
{
	cancel_delayed_work_sync(&poll);
	regmap_update_bits(map, ACCDET_CON1, BIT(2), 0);
	input_unregister_device(idev);
	put_device(pdev_ref);
}

module_init(mt6357_jack_init);
module_exit(mt6357_jack_exit);
MODULE_DESCRIPTION("MT6357 ACCDET headphone plug detection (polling)");
MODULE_LICENSE("GPL");
