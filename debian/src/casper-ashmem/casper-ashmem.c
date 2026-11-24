// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal /dev/ashmem for Android userspace (libcutils) on a mainline kernel.
 * Same ioctl ABI as the old staging driver; regions are shmem files. Pin/unpin are
 * no-ops (memory is never purged), which is what a non-Android host wants.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define ASHMEM_NAME_LEN		256
#define ASHMEM_NAME_PREFIX	"dev/ashmem/"
#define ASHMEM_NAME_PREFIX_LEN	(sizeof(ASHMEM_NAME_PREFIX) - 1)
#define ASHMEM_FULL_NAME_LEN	(ASHMEM_NAME_LEN + ASHMEM_NAME_PREFIX_LEN)

struct ashmem_pin { __u32 offset; __u32 len; };

#define __ASHMEMIOC		0x77
#define ASHMEM_SET_NAME		_IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME		_IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE		_IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK	_IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN		_IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN		_IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS	_IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES	_IO(__ASHMEMIOC, 10)

#define ASHMEM_IS_PINNED	1

struct ashmem_area {
	char name[ASHMEM_FULL_NAME_LEN];
	struct mutex lock;
	size_t size;
	unsigned long prot_mask;
	struct file *file;
};

static int ashmem_open(struct inode *inode, struct file *file)
{
	struct ashmem_area *a = kzalloc(sizeof(*a), GFP_KERNEL);

	if (!a)
		return -ENOMEM;
	mutex_init(&a->lock);
	strcpy(a->name, ASHMEM_NAME_PREFIX);
	a->prot_mask = PROT_READ | PROT_WRITE | PROT_EXEC;
	file->private_data = a;
	return 0;
}

static int ashmem_release(struct inode *inode, struct file *file)
{
	struct ashmem_area *a = file->private_data;

	if (a->file)
		fput(a->file);
	kfree(a);
	return 0;
}

static int ashmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ashmem_area *a = file->private_data;
	vm_flags_t clear = 0;
	int ret = 0;

	mutex_lock(&a->lock);
	if (!a->size) {
		ret = -EINVAL;
		goto out;
	}
	/* requested protections must be allowed by the mask */
	if (((vma->vm_flags & VM_READ) && !(a->prot_mask & PROT_READ)) ||
	    ((vma->vm_flags & VM_WRITE) && !(a->prot_mask & PROT_WRITE)) ||
	    ((vma->vm_flags & VM_EXEC) && !(a->prot_mask & PROT_EXEC))) {
		ret = -EPERM;
		goto out;
	}
	if (!(a->prot_mask & PROT_READ))
		clear |= VM_MAYREAD;
	if (!(a->prot_mask & PROT_WRITE))
		clear |= VM_MAYWRITE;
	if (!(a->prot_mask & PROT_EXEC))
		clear |= VM_MAYEXEC;
	if (clear)
		vm_flags_clear(vma, clear);

	if (!a->file) {
		struct file *f = shmem_file_setup(a->name, a->size, vma->flags);

		if (IS_ERR(f)) {
			ret = PTR_ERR(f);
			goto out;
		}
		f->f_mode |= FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE;
		a->file = f;
	}
	vma_set_file(vma, a->file);
	ret = vfs_mmap(a->file, vma);
out:
	mutex_unlock(&a->lock);
	return ret;
}

static long ashmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ashmem_area *a = file->private_data;
	long ret = -ENOTTY;

	switch (cmd) {
	case ASHMEM_SET_NAME: {
		char n[ASHMEM_NAME_LEN];

		ret = strncpy_from_user(n, (const char __user *)arg, ASHMEM_NAME_LEN);
		if (ret < 0)
			break;
		n[ASHMEM_NAME_LEN - 1] = 0;
		mutex_lock(&a->lock);
		if (a->file)
			ret = -EINVAL;
		else {
			strscpy(a->name + ASHMEM_NAME_PREFIX_LEN, n, ASHMEM_NAME_LEN);
			ret = 0;
		}
		mutex_unlock(&a->lock);
		break;
	}
	case ASHMEM_GET_NAME: {
		char n[ASHMEM_NAME_LEN];

		mutex_lock(&a->lock);
		if (a->name[ASHMEM_NAME_PREFIX_LEN])
			strscpy(n, a->name + ASHMEM_NAME_PREFIX_LEN, ASHMEM_NAME_LEN);
		else
			strscpy(n, a->name, ASHMEM_NAME_LEN);
		mutex_unlock(&a->lock);
		ret = copy_to_user((void __user *)arg, n, strlen(n) + 1) ? -EFAULT : 0;
		break;
	}
	case ASHMEM_SET_SIZE:
		mutex_lock(&a->lock);
		if (a->file)
			ret = -EINVAL;
		else {
			a->size = arg;
			ret = 0;
		}
		mutex_unlock(&a->lock);
		break;
	case ASHMEM_GET_SIZE:
		ret = a->size;
		break;
	case ASHMEM_SET_PROT_MASK:
		/* may only drop permissions */
		if ((arg & ~a->prot_mask & (PROT_READ | PROT_WRITE | PROT_EXEC))) {
			ret = -EINVAL;
			break;
		}
		a->prot_mask = arg;
		ret = 0;
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = a->prot_mask;
		break;
	case ASHMEM_PIN:
	case ASHMEM_UNPIN:
		ret = a->file ? 0 : -EINVAL;	/* 0 = ASHMEM_NOT_PURGED */
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ASHMEM_IS_PINNED;
		break;
	case ASHMEM_PURGE_ALL_CACHES:
		ret = 0;
		break;
	}
	return ret;
}

static const struct file_operations ashmem_fops = {
	.owner = THIS_MODULE,
	.open = ashmem_open,
	.release = ashmem_release,
	.mmap = ashmem_mmap,
	.unlocked_ioctl = ashmem_ioctl,
	.compat_ioctl = ashmem_ioctl,
};

static struct miscdevice ashmem_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &ashmem_fops,
	.mode = 0666,
};

module_misc_device(ashmem_misc);
MODULE_DESCRIPTION("Minimal ashmem for Android userspace on mainline");
MODULE_LICENSE("GPL");
