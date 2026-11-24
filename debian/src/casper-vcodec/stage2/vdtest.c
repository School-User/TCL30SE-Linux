// Stage 2: decode an Annex-B H.264 file with the vendor MediaTek decoder (libvcodecdrv)
// and dump each displayed frame buffer raw (MTK block layout) to an output file.
// usage: vdtest in.h264 out.raw [maxframes]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include "../mtk-ref/val_types_public.h"
#include "../mtk-ref/vdec_drv_if_public.h"

#define NFB 10

typedef VDEC_DRV_MRESULT_T (*create_t)(VAL_HANDLE_T *, VDEC_DRV_VIDEO_FORMAT_T);
typedef VDEC_DRV_MRESULT_T (*h_t)(VAL_HANDLE_T);
typedef VDEC_DRV_MRESULT_T (*init_t)(VAL_HANDLE_T, VDEC_DRV_RINGBUF_T *, VDEC_DRV_SEQINFO_T *);
typedef VDEC_DRV_MRESULT_T (*param_t)(VAL_HANDLE_T, int, void *, void *);
typedef VDEC_DRV_MRESULT_T (*dec_t)(VAL_HANDLE_T, VDEC_DRV_RINGBUF_T *, VDEC_DRV_FRAMEBUF_T *);
typedef VAL_RESULT_T (*mem_t)(VAL_MEMORY_T *, VAL_UINT32_T);
typedef VAL_RESULT_T (*cache_t)(VAL_MEMORY_T *, VAL_UINT32_T, VAL_UINT32_T);

static mem_t mem_alloc;

static void *sym(void *lib, const char *n)
{
	void *p = dlsym(lib, n);
	if (!p) { printf("missing %s\n", n); exit(1); }
	return p;
}

static int alloc(VAL_MEMORY_T *m, unsigned size)
{
	memset(m, 0, sizeof(*m));
	m->eMemType = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
	m->u4MemSize = size;
	m->eAlignment = VAL_MEM_ALIGN_512;
	m->eMemCodec = VAL_MEM_CODEC_FOR_VDEC;
	int r = mem_alloc(m, sizeof(*m));
	printf("alloc %u -> %d va=%p pa=%p\n", size, r, m->pvMemVa, m->pvMemPa);
	return r;
}

/* next Annex-B start code at or after p; returns offset of the 00 00 01 */
static long next_sc(const unsigned char *b, long n, long p)
{
	for (; p + 3 <= n; p++)
		if (!b[p] && !b[p + 1] && b[p + 2] == 1) return p;
	return n;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, 0, _IONBF, 0);
	if (argc < 3) { printf("usage: vdtest in.h264 out.raw [maxframes]\n"); return 1; }
	int maxf = argc > 3 ? atoi(argv[3]) : 1000;

	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror("in"); return 1; }
	fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
	unsigned char *bs = malloc(n);
	if (fread(bs, 1, n, f) != (size_t)n) { perror("read"); return 1; }
	fclose(f);
	FILE *out = fopen(argv[2], "wb");

	void *lib = dlopen("libvcodecdrv.so", RTLD_NOW);
	if (!lib) { printf("dlopen: %s\n", dlerror()); return 1; }
	void *ul = dlopen("libvcodec_utility.so", RTLD_NOW);
	if (!ul) { printf("dlopen util: %s\n", dlerror()); return 1; }
	create_t cr = sym(lib, "eVDecDrvCreate");
	h_t rel = sym(lib, "eVDecDrvRelease"), deinit = sym(lib, "eVDecDrvDeInit");
	init_t init = sym(lib, "eVDecDrvInit");
	param_t getp = sym(lib, "eVDecDrvGetParam"), setp = sym(lib, "eVDecDrvSetParam");
	dec_t dec = sym(lib, "eVDecDrvDecode");
	mem_alloc = sym(ul, "eVideoMemAlloc");
	mem_t mem_free = sym(ul, "eVideoMemFree");
	cache_t inval = sym(ul, "eVideoInvalidateCache"), flush = sym(ul, "eVideoFlushCache");
	printf("sizeof ring=%zu fb=%zu seq=%zu mem=%zu\n", sizeof(VDEC_DRV_RINGBUF_T),
	       sizeof(VDEC_DRV_FRAMEBUF_T), sizeof(VDEC_DRV_SEQINFO_T), sizeof(VAL_MEMORY_T));

	VAL_MEMORY_T bsm;
	if (alloc(&bsm, 1 << 20)) return 1;
	unsigned char *bva = bsm.pvMemVa;

	/* access units: each ends after a VCL NAL (single slice per picture) */
	long au_start[1024], au_end[1024]; int nau = 0;
	long hdr_end = 0; /* end of leading SPS/PPS */
	long p = next_sc(bs, n, 0), s = p;
	while (p < n && nau < 1024) {
		long q = next_sc(bs, n, p + 3);
		int t = bs[p + 3] & 0x1f;
		if ((t == 7 || t == 8) && !nau && s == p) hdr_end = q;
		if (t == 1 || t == 5) { au_start[nau] = s; au_end[nau++] = q; s = q; }
		p = q;
	}
	printf("file %ld bytes, header %ld bytes, %d access units\n", n, hdr_end, nau);

	VAL_HANDLE_T h = 0;
	int r = cr(&h, VDEC_DRV_VIDEO_FORMAT_H264);
	printf("create -> %d h=%#lx\n", r, h);
	if (r) return 1;

	VDEC_DRV_RINGBUF_T ring;
	memset(&ring, 0, sizeof(ring));
	ring.rBase.u4VA = (VAL_ULONG_T)bsm.pvMemVa;
	ring.rBase.u4PA = (VAL_ULONG_T)bsm.pvMemPa;
	ring.rBase.u4Size = bsm.u4MemSize;
	long init_len = getenv("INIT_FULL") ? au_end[0] : hdr_end;
	memcpy(bva, bs, init_len);
	flush(&bsm, sizeof(bsm), 0);
	ring.u4Read = (VAL_ULONG_T)bva;
	ring.u4Write = (VAL_ULONG_T)bva + init_len;

	VDEC_DRV_SEQINFO_T seq;
	memset(&seq, 0, sizeof(seq));
	r = init(h, &ring, &seq);
	printf("init(%ld bytes) -> %d buf %ux%u pic %ux%u\n", init_len, r, seq.u4Width, seq.u4Height,
	       seq.u4PicWidth, seq.u4PicHeight);
	if (r) goto out;

	unsigned fmt = 0;
	r = getp(h, VDEC_DRV_GET_TYPE_GET_PIXEL_FORMAT, NULL, &fmt);
	printf("pixel format -> %d fmt=%u\n", r, fmt);
	unsigned dpb = 0;
	r = getp(h, VDEC_DRV_GET_TYPE_QUERY_VIDEO_DPB_SIZE, NULL, &dpb);
	printf("dpb size -> %d dpb=%u\n", r, dpb);

	unsigned bw = (seq.u4Width + 63) & ~63, bh = (seq.u4Height + 63) & ~63;
	unsigned fbsize = bw * bh * 3 / 2 + 4096;
	VAL_MEMORY_T fbm[NFB];
	/* the vendor FRAMEBUF_T is larger than the public header's: give each one a padded slot */
	struct slot { VDEC_DRV_FRAMEBUF_T f; unsigned char pad[392]; };
	static struct slot sl[NFB];
	memset(sl, 0xAA, sizeof(sl));
#define FB(i) (&sl[i].f)
	int busy[NFB] = {0};
	for (int i = 0; i < NFB; i++) {
		if (alloc(&fbm[i], fbsize)) return 1;
		memset(FB(i), 0, sizeof(*FB(i)));
		FB(i)->rBaseAddr.u4VA = (VAL_ULONG_T)fbm[i].pvMemVa;
		FB(i)->rBaseAddr.u4PA = (VAL_ULONG_T)fbm[i].pvMemPa;
		FB(i)->rBaseAddr.u4Size = fbsize;
		FB(i)->u4BufWidth = seq.u4Width;
		FB(i)->u4BufHeight = seq.u4Height;
	}

	int shown = 0, nodump = getenv("NODUMP") != NULL;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	for (int a = 0; a <= nau && a < maxf + 1; a++) {
		int k;
		for (k = 0; k < NFB && busy[k]; k++) ;
		if (k == NFB) { printf("no free frame buffer\n"); break; }
		VDEC_DRV_RINGBUF_T *rp = NULL;
		if (a < nau && a < maxf) {
			long st = a ? au_start[a] : (getenv("INIT_FULL") ? 0 : hdr_end);
			if (!a && getenv("AU0_HDR")) st = 0;
			long len = au_end[a] - st;
			memcpy(bva, bs + st, len);
			flush(&bsm, sizeof(bsm), 0);
			ring.u4Read = (VAL_ULONG_T)bva;
			ring.u4Write = (VAL_ULONG_T)bva + len;
			ring.u4Timestamp = a;
			rp = &ring;
		}
		busy[k] = 1;
		r = dec(h, rp, FB(k));
		if (r || getenv("VERBOSE")) printf("decode au%d (fb%d) -> %d\n", a, k, r);
		if (r) break;
		for (;;) {
			VDEC_DRV_FRAMEBUF_T *d = NULL;
			getp(h, VDEC_DRV_GET_TYPE_DISP_FRAME_BUFFER, NULL, &d);
			if (!d) break;
			int i = (int)(((char *)d - (char *)sl) / (long)sizeof(sl[0]));
			if (getenv("VERBOSE")) printf("  disp fb%d ts=%u buf %ux%u disp %ux%u pitch %u status %#x\n", i, d->u4Timestamp,
			       d->u4BufWidth, d->u4BufHeight, d->u4DispWidth, d->u4DispHeight, d->u4DispPitch,
			       d->u4FrameBufferStatus);
			if (i >= 0 && i < NFB) {
				if (!nodump) {
					inval(&fbm[i], sizeof(fbm[i]), 0);
					fwrite(fbm[i].pvMemVa, 1, bw * bh * 3 / 2, out);
				}
				shown++;
			}
		}
		for (;;) {
			VDEC_DRV_FRAMEBUF_T *d = NULL;
			getp(h, VDEC_DRV_GET_TYPE_FREE_FRAME_BUFFER, NULL, &d);
			if (!d) break;
			int i = (int)(((char *)d - (char *)sl) / (long)sizeof(sl[0]));
			if (i >= 0 && i < NFB) busy[i] = 0;
		}
		if (!rp) break;
	}
	for (int i = 0; i < NFB; i++) {
		int last = -1;
		for (int j = 0; j < (int)sizeof(sl[i].pad); j++) if (sl[i].pad[j] != 0xAA) last = j;
		if (last >= 0) printf("fb%d: vendor wrote up to byte %zu of the slot\n", i, sizeof(sl[i].f) + last + 1);
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	printf("%d frames in %.3f s = %.1f fps%s\n", shown, sec, shown / sec, nodump ? " (no dump)" : "");
	printf("frames shown: %d (buffer %ux%u, %u bytes each)\n", shown, bw, bh, bw * bh * 3 / 2);
	(void)setp;
out:
	printf("deinit -> %d\n", deinit(h));
	printf("release -> %d\n", rel(h));
	fclose(out);
	mem_free(&bsm, sizeof(bsm));
	return 0;
}
