// Exercise /dev/Vcodec like libvcodec_utility does: LOCKHW (H.264 decoder), WAITISR (expect timeout), UNLOCKHW.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#define M 'M'
#define VCODEC_WAITISR _IOW(M, 0x0b, unsigned int)
#define VCODEC_LOCKHW _IOW(M, 0x0d, unsigned int)
#define VCODEC_UNLOCKHW _IOW(M, 0x33, unsigned int)
#define VCODEC_GET_CORE_NUMBER _IOW(M, 0x31, unsigned int)
struct lk { void *h; unsigned hs; void *lock; unsigned to; void *res; unsigned rs; unsigned type; char sec; };
struct isr { void *h; unsigned hs; unsigned type; void *f; void *res; unsigned rs; unsigned to; unsigned n; unsigned st[16]; };
int main(void) {
	static char inst[64];
	int fd = open("/dev/Vcodec", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open"); return 1; }
	printf("sizeof lock=%zu isr=%zu\n", sizeof(struct lk), sizeof(struct isr));
	int n = 0; printf("GET_CORE_NUMBER rc=%d n=%d\n", ioctl(fd, VCODEC_GET_CORE_NUMBER, &n), n);
	struct lk l = { .h = inst, .hs = 64, .to = 1000, .type = 6 };
	printf("LOCKHW rc=%d\n", ioctl(fd, VCODEC_LOCKHW, &l));
	volatile uint32_t *r = mmap(0, 0x50000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x17000000);
	if (r == MAP_FAILED) perror("mmap"); else printf("mmap ok: gcon=0x%08x vdec_misc[0xA4]=0x%08x\n", r[0], r[(0x45000 + 0xA4) / 4]);
	struct isr s = { .h = inst, .hs = 64, .type = 6, .to = 200 };
	int rc = ioctl(fd, VCODEC_WAITISR, &s); printf("WAITISR rc=%d errno=%d (expect timeout)\n", rc, rc ? errno : 0);
	printf("UNLOCKHW rc=%d\n", ioctl(fd, VCODEC_UNLOCKHW, &l));
	printf("UNLOCKHW again rc=%d errno=%d (expect EFAULT)\n", ioctl(fd, VCODEC_UNLOCKHW, &l), errno);
	return 0;
}
