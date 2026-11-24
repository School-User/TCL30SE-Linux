// SPDX-License-Identifier: GPL-2.0
/* Runtime DT overlay that adds the MT6765 GPU node to a kernel whose DTB does not have it yet. */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include "gpu_dtbo.h"

static int ovcs_id;

static int __init gpudt_init(void)
{
	int ret = of_overlay_fdt_apply((void *)gpu_dtbo, sizeof(gpu_dtbo), &ovcs_id, NULL);
	pr_info("casper-gpu-dt: overlay apply = %d id %d\n", ret, ovcs_id);
	return ret;
}
static void __exit gpudt_exit(void) { of_overlay_remove(&ovcs_id); }
module_init(gpudt_init);
module_exit(gpudt_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Adds GPU DT node at runtime");
