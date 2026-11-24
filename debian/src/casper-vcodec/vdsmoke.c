// 32-bit bionic smoke test: open the vendor H.264 hardware decoder driver library and create/release an instance.
#include <stdio.h>
#include <dlfcn.h>
typedef int (*create_t)(void **h, unsigned fmt);
typedef int (*release_t)(void *h);
int main(void) {
	setvbuf(stdout, 0, _IONBF, 0);
	void *lib = dlopen("libvcodecdrv.so", RTLD_NOW);
	if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
	create_t cr = (create_t)dlsym(lib, "eVDecDrvCreate");
	release_t rl = (release_t)dlsym(lib, "eVDecDrvRelease");
	printf("eVDecDrvCreate=%p eVDecDrvRelease=%p\n", (void *)cr, (void *)rl);
	void *h = 0;
	int r = cr(&h, 1 << 8 /* VDEC_DRV_VIDEO_FORMAT_H264 */);
	printf("create(H264) -> %d handle=%p\n", r, h);
	if (h) printf("release -> %d\n", rl(h));
	return 0;
}
