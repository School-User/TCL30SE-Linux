// SPDX-License-Identifier: GPL-2.0
/* Android ION compatibility device (/dev/ion) for the MTK/PowerVR vendor userspace on a mainline kernel.
 * Legacy (handle based) ION ABI of the stock 4.19 kernel + the MediaTek ION_CMD_SYSTEM custom ioctls.
 * Buffers are plain 4K pages below 4 GB (the GE8320 only reaches 32-bit physical addresses) exported as dma-bufs. */
#include <linux/module.h>
#include <linux/compat.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/mm.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/iosys-map.h>
#include <linux/vmalloc.h>

MODULE_IMPORT_NS("DMA_BUF");

struct ion_buf { struct page **pages; unsigned int npages; size_t len; void *contig; dma_addr_t dma; };	/* contig: CMA block via dma_alloc_pages() (camera heap) */
static struct device *ion_dev_ptr;

/* ---- legacy ION uapi (android-4.19 drivers/staging/android/uapi/ion.h) ---- */
struct ion_allocation_data { size_t len; size_t align; unsigned int heap_id_mask; unsigned int flags; int handle; };
struct ion_fd_data { int handle; int fd; };
struct ion_handle_data { int handle; };
struct ion_custom_data { unsigned int cmd; unsigned long arg; };
#define ION_HEAP_CAMERA_MASK	0x1000	/* libion_mtk ion_alloc_camera() (video codec buffers) */
#define ION_IOC_ALLOC	_IOWR('I', 0, struct ion_allocation_data)
#define ION_IOC_FREE	_IOWR('I', 1, struct ion_handle_data)
#define ION_IOC_MAP	_IOWR('I', 2, struct ion_fd_data)
#define ION_IOC_SHARE	_IOWR('I', 4, struct ion_fd_data)
#define ION_IOC_IMPORT	_IOWR('I', 5, struct ion_fd_data)
#define ION_IOC_SYNC	_IOWR('I', 7, struct ion_fd_data)
#define ION_IOC_CUSTOM	_IOWR('I', 6, struct ion_custom_data)

/* ---- dma-buf exporter ---- */
static struct sg_table *ion_map(struct dma_buf_attachment *a, enum dma_data_direction dir)
{
	struct ion_buf *b = a->dmabuf->priv;
	struct sg_table *st = kmalloc(sizeof(*st), GFP_KERNEL);
	if (!st) return ERR_PTR(-ENOMEM);
	if (sg_alloc_table_from_pages(st, b->pages, b->npages, 0, b->len, GFP_KERNEL)) { kfree(st); return ERR_PTR(-ENOMEM); }
	if (dma_map_sgtable(a->dev, st, dir, DMA_ATTR_SKIP_CPU_SYNC)) { sg_free_table(st); kfree(st); return ERR_PTR(-ENOMEM); }
	return st;
}
static void ion_unmap(struct dma_buf_attachment *a, struct sg_table *st, enum dma_data_direction dir)
{
	dma_unmap_sgtable(a->dev, st, dir, DMA_ATTR_SKIP_CPU_SYNC); sg_free_table(st); kfree(st);
}
static void ion_release(struct dma_buf *d)
{
	struct ion_buf *b = d->priv; unsigned int i;
	if (b->contig) dma_free_pages(ion_dev_ptr, b->len, virt_to_page(b->contig), b->dma, DMA_BIDIRECTIONAL);
	else for (i = 0; i < b->npages; i++) __free_page(b->pages[i]);
	kvfree(b->pages); kfree(b);
}
static int ion_mmap(struct dma_buf *d, struct vm_area_struct *vma)
{
	struct ion_buf *b = d->priv; unsigned long off = vma->vm_pgoff, addr = vma->vm_start; unsigned int i;
	if (off + vma_pages(vma) > b->npages) return -EINVAL;
	/* contiguous: may be a non-compound buddy block (CMA exhausted) whose tail pages vm_insert_page() refuses */
	if (b->contig)
		return remap_pfn_range(vma, vma->vm_start, page_to_pfn(b->pages[off]), vma->vm_end - vma->vm_start, vma->vm_page_prot);
	for (i = off; addr < vma->vm_end; i++, addr += PAGE_SIZE) {
		int r = vm_insert_page(vma, addr, b->pages[i]);
		if (r) return r;
	}
	return 0;
}
static int ion_vmap(struct dma_buf *d, struct iosys_map *map)
{
	struct ion_buf *b = d->priv; void *v = vmap(b->pages, b->npages, VM_MAP, PAGE_KERNEL);
	if (!v) return -ENOMEM; iosys_map_set_vaddr(map, v); return 0;
}
static void ion_vunmap(struct dma_buf *d, struct iosys_map *map) { vunmap(map->vaddr); }
static int ion_begin_cpu(struct dma_buf *d, enum dma_data_direction dir)
{
	struct ion_buf *b = d->priv; unsigned int i;
	for (i = 0; i < b->npages; i++) dma_sync_single_for_cpu(ion_dev_ptr, page_to_phys(b->pages[i]), PAGE_SIZE, dir);
	return 0;
}
static int ion_end_cpu(struct dma_buf *d, enum dma_data_direction dir)
{
	struct ion_buf *b = d->priv; unsigned int i;
	for (i = 0; i < b->npages; i++) dma_sync_single_for_device(ion_dev_ptr, page_to_phys(b->pages[i]), PAGE_SIZE, dir);
	return 0;
}
static const struct dma_buf_ops ion_ops = { .map_dma_buf = ion_map, .unmap_dma_buf = ion_unmap, .release = ion_release, .mmap = ion_mmap,
	.vmap = ion_vmap, .vunmap = ion_vunmap, .begin_cpu_access = ion_begin_cpu, .end_cpu_access = ion_end_cpu };

static struct dma_buf *ion_new(size_t len, bool contig)
{
	struct ion_buf *b; struct dma_buf *d; DEFINE_DMA_BUF_EXPORT_INFO(exp); unsigned int i;
	len = PAGE_ALIGN(len);
	b = kzalloc(sizeof(*b), GFP_KERNEL); if (!b) return ERR_PTR(-ENOMEM);
	b->len = len; b->npages = len >> PAGE_SHIFT;
	b->pages = kvcalloc(b->npages, sizeof(*b->pages), GFP_KERNEL);
	if (!b->pages) { kfree(b); return ERR_PTR(-ENOMEM); }
	if (contig) {
		/* hardware video decoder buffers: physically contiguous, below 4 GB (it takes 32-bit physical addresses) */
		/* dma-direct takes these from CMA, so blocks above the 4 MB buddy limit work (the decoder wants 5 MB+) */
		struct page *pg = dma_alloc_pages(ion_dev_ptr, len, &b->dma, DMA_BIDIRECTIONAL, GFP_KERNEL | __GFP_NOWARN);
		if (!pg) { pr_info("casper-ion: no contiguous %zu bytes\n", len); kvfree(b->pages); kfree(b); return ERR_PTR(-ENOMEM); }
		b->contig = page_address(pg);
		for (i = 0; i < b->npages; i++) b->pages[i] = pg + i;
		dma_sync_single_for_device(ion_dev_ptr, b->dma, len, DMA_BIDIRECTIONAL);
		i = b->npages;
	} else for (i = 0; i < b->npages; i++) {
		b->pages[i] = alloc_page(GFP_KERNEL | __GFP_DMA32 | __GFP_ZERO);
		if (!b->pages[i]) goto fail;
		dma_sync_single_for_device(ion_dev_ptr, page_to_phys(b->pages[i]), PAGE_SIZE, DMA_BIDIRECTIONAL);
	}
	exp.ops = &ion_ops; exp.size = len; exp.flags = O_RDWR; exp.priv = b;
	d = dma_buf_export(&exp);
	if (IS_ERR(d)) goto fail;
	return d;
fail:
	if (b->contig) dma_free_pages(ion_dev_ptr, len, virt_to_page(b->contig), b->dma, DMA_BIDIRECTIONAL);
	else while (i--) __free_page(b->pages[i]);
	kvfree(b->pages); kfree(b); return ERR_PTR(-ENOMEM);
}

/* ---- per open client: handle -> dma_buf ---- */
struct ion_client { struct idr handles; struct mutex lock; };
static int ion_open(struct inode *i, struct file *f)
{
	struct ion_client *c = kzalloc(sizeof(*c), GFP_KERNEL); if (!c) return -ENOMEM;
	idr_init(&c->handles); mutex_init(&c->lock); f->private_data = c; return 0;
}
static int ion_close(struct inode *i, struct file *f)
{
	struct ion_client *c = f->private_data; struct dma_buf *d; int id;
	idr_for_each_entry(&c->handles, d, id) dma_buf_put(d);
	idr_destroy(&c->handles); kfree(c); return 0;
}
static struct dma_buf *get_h(struct ion_client *c, int h)
{
	struct dma_buf *d; mutex_lock(&c->lock); d = idr_find(&c->handles, h); mutex_unlock(&c->lock); return d;
}
static int add_h(struct ion_client *c, struct dma_buf *d)
{
	int id; mutex_lock(&c->lock); id = idr_alloc(&c->handles, d, 1, 0, GFP_KERNEL); mutex_unlock(&c->lock); return id;
}

/* MediaTek ION_CMD_SYSTEM / ION_CMD_MULTIMEDIA. 64-bit ion_sys_data: u32 sys_cmd | pad | handle union @8 | u32 phy_addr @16 | u64 len @24.
 * 32-bit (union still 8-byte aligned, seen on the wire): u32 sys_cmd | pad | handle @8 | u32 phy_addr @12 | u32 len @16. */
#define ION_CMD_SYSTEM		0
#define ION_CMD_MULTIMEDIA	1
#define ION_SYS_CACHE_SYNC	0
#define ION_SYS_GET_PHYS	1
static int sync_dbg;
static long ion_custom_do(struct ion_client *c, u32 cmd, void __user *uarg, bool compat)
{
	u32 w[8] = {};
	size_t n = 32;	/* read generously; layouts below */

	if (uarg && copy_from_user(w, uarg, n)) return -EFAULT;
	if (cmd == ION_CMD_SYSTEM && w[0] == ION_SYS_GET_PHYS) {
		int h = (int)w[2];	/* the union is 8-byte aligned in both ABIs */
		struct dma_buf *d = get_h(c, h);
		struct ion_buf *b = d ? d->priv : NULL;
		phys_addr_t pa;

		if (!b || d->ops != &ion_ops || !b->contig) {
			pr_info("casper-ion: GET_PHYS handle %d: %s\n", h, !b ? "bad handle" : "not contiguous");
			return -EINVAL;
		}
		pa = b->dma;	/* no IOMMU: dma address == physical */
		if (compat) { w[3] = (u32)pa; w[4] = (u32)b->len; }	/* 32-bit: phy_addr @12, len @16 */
		else { w[4] = (u32)pa; w[5] = 0; w[6] = (u32)b->len; w[7] = (u32)((u64)b->len >> 32); }
		return copy_to_user(uarg, w, n) ? -EFAULT : 0;
	}
	if (cmd == ION_CMD_MULTIMEDIA)
		return 0;	/* ION_MM_CONFIG_BUFFER etc.: M4U port setup - no IOMMU here, buffers are physical */
	if (cmd == ION_CMD_SYSTEM && w[0] == ION_SYS_CACHE_SYNC) {
		/* ion_sys_cache_sync_param: handle union @8 (both ABIs). Whatever the type asked, clean + invalidate the whole buffer. */
		struct dma_buf *d = get_h(c, (int)w[2]);
		struct ion_buf *b = d ? d->priv : NULL;
		if (b && d->ops == &ion_ops) {
			ion_end_cpu(d, DMA_BIDIRECTIONAL);
			ion_begin_cpu(d, DMA_BIDIRECTIONAL);
		} else if (sync_dbg++ < 4)
			pr_info("casper-ion: CACHE_SYNC raw %08x %08x %08x %08x %08x %08x\n", w[0], w[1], w[2], w[3], w[4], w[5]);
		return 0;
	}
	if (cmd == ION_CMD_SYSTEM && (w[0] == 4 /* SET_CLIENT_NAME */ || w[0] == 3 /* SET_HANDLE_BACKTRACE */))
		return 0;
	pr_info("casper-ion: CUSTOM cmd=%u sys_cmd=%u unhandled (comm %s)\n", cmd, w[0], current->comm);
	return -ENOTTY;
}

static long ion_custom(struct ion_client *c, unsigned long arg)
{
	struct ion_custom_data cd;
	if (copy_from_user(&cd, (void __user *)arg, sizeof(cd))) return -EFAULT;
	return ion_custom_do(c, cd.cmd, (void __user *)cd.arg, false);
}

static long ion_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct ion_client *c = f->private_data; struct dma_buf *d;
	switch (cmd) {
	case ION_IOC_ALLOC: {
		struct ion_allocation_data a; int h;
		if (copy_from_user(&a, (void __user *)arg, sizeof(a))) return -EFAULT;
		d = ion_new(a.len, a.heap_id_mask & ION_HEAP_CAMERA_MASK); if (IS_ERR(d)) return PTR_ERR(d);
		h = add_h(c, d); if (h < 0) { dma_buf_put(d); return h; }
		a.handle = h; pr_info("casper-ion: alloc len=%zu mask=0x%x flags=0x%x -> handle %d\n", a.len, a.heap_id_mask, a.flags, h);
		return copy_to_user((void __user *)arg, &a, sizeof(a)) ? -EFAULT : 0; }
	case ION_IOC_FREE: {
		struct ion_handle_data hd;
		if (copy_from_user(&hd, (void __user *)arg, sizeof(hd))) return -EFAULT;
		mutex_lock(&c->lock); d = idr_remove(&c->handles, hd.handle); mutex_unlock(&c->lock);
		if (!d) return -EINVAL; dma_buf_put(d); return 0; }
	case ION_IOC_MAP:
	case ION_IOC_SHARE: {
		struct ion_fd_data fd;
		if (copy_from_user(&fd, (void __user *)arg, sizeof(fd))) return -EFAULT;
		d = get_h(c, fd.handle); if (!d) return -EINVAL;
		get_dma_buf(d); fd.fd = dma_buf_fd(d, O_CLOEXEC); if (fd.fd < 0) { dma_buf_put(d); return fd.fd; }
		return copy_to_user((void __user *)arg, &fd, sizeof(fd)) ? -EFAULT : 0; }
	case ION_IOC_IMPORT: {
		struct ion_fd_data fd; int h;
		if (copy_from_user(&fd, (void __user *)arg, sizeof(fd))) return -EFAULT;
		d = dma_buf_get(fd.fd); if (IS_ERR(d)) return PTR_ERR(d);
		h = add_h(c, d); if (h < 0) { dma_buf_put(d); return h; }
		fd.handle = h; return copy_to_user((void __user *)arg, &fd, sizeof(fd)) ? -EFAULT : 0; }
	case ION_IOC_SYNC: {
		struct ion_fd_data fd; struct dma_buf *db;
		if (copy_from_user(&fd, (void __user *)arg, sizeof(fd))) return -EFAULT;
		db = dma_buf_get(fd.fd); if (IS_ERR(db)) return PTR_ERR(db);
		dma_buf_begin_cpu_access(db, DMA_BIDIRECTIONAL); dma_buf_end_cpu_access(db, DMA_BIDIRECTIONAL); dma_buf_put(db); return 0; }
	case ION_IOC_CUSTOM:
		return ion_custom(c, arg);
	}
	pr_info("casper-ion: unhandled ioctl cmd=0x%08x (type '%c' nr %u size %u) comm=%s\n", cmd, _IOC_TYPE(cmd), _IOC_NR(cmd), _IOC_SIZE(cmd), current->comm);
	return -ENOTTY;
}
#ifdef CONFIG_COMPAT
/* 32-bit (armv7) vendor userspace: size_t/unsigned long are 32 bits, so the ioctl numbers differ too. */
struct ion_allocation_data32 { u32 len; u32 align; u32 heap_id_mask; u32 flags; s32 handle; };
struct ion_custom_data32 { u32 cmd; u32 arg; };
#define ION_IOC_ALLOC32		_IOWR('I', 0, struct ion_allocation_data32)
#define ION_IOC_CUSTOM32	_IOWR('I', 6, struct ion_custom_data32)
static long ion_compat_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct ion_client *c = f->private_data;

	switch (cmd) {
	case ION_IOC_ALLOC32: {
		struct ion_allocation_data32 a; struct dma_buf *d; int h;
		if (copy_from_user(&a, compat_ptr(arg), sizeof(a))) return -EFAULT;
		d = ion_new(a.len, a.heap_id_mask & ION_HEAP_CAMERA_MASK); if (IS_ERR(d)) return PTR_ERR(d);
		h = add_h(c, d); if (h < 0) { dma_buf_put(d); return h; }
		a.handle = h;
		return copy_to_user(compat_ptr(arg), &a, sizeof(a)) ? -EFAULT : 0; }
	case ION_IOC_CUSTOM32: {
		struct ion_custom_data32 cd;
		if (copy_from_user(&cd, compat_ptr(arg), sizeof(cd))) return -EFAULT;
		return ion_custom_do(c, cd.cmd, compat_ptr(cd.arg), true); }
	default:	/* FREE/MAP/SHARE/IMPORT/SYNC: same layout (two ints) for both ABIs */
		return ion_ioctl(f, cmd, (unsigned long)compat_ptr(arg));
	}
}
#endif
static const struct file_operations ion_fops = { .owner = THIS_MODULE, .open = ion_open, .release = ion_close, .unlocked_ioctl = ion_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = ion_compat_ioctl,
#endif
};
static struct miscdevice ion_misc = { .minor = MISC_DYNAMIC_MINOR, .name = "ion", .fops = &ion_fops, .mode = 0666 };
static int __init ion_init(void)
{
	int r = misc_register(&ion_misc); if (r) return r;
	ion_dev_ptr = ion_misc.this_device;
	dma_coerce_mask_and_coherent(ion_dev_ptr, DMA_BIT_MASK(32));
	return 0;
}
static void __exit ion_exit(void) { misc_deregister(&ion_misc); }
module_init(ion_init); module_exit(ion_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ION compat shim");
