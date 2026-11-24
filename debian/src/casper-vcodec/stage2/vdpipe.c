// vdpipe [h264|vp9]: Annex-B H.264 (default) or IVF-framed VP9 on stdin -> MT6765 hardware decoder (vendor libvcodecdrv) -> raw NV12 frames on stdout.
// 32-bit bionic, runs in the Android chroot. Prints "vdpipe: WxH" to stderr once the stream size is known.
// Frames are cropped to the coded picture size (seq PicWidth x PicHeight) and emitted in display order.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <time.h>
#include <arm_neon.h>
#include "../mtk-ref/val_types_public.h"
#include "../mtk-ref/vdec_drv_if_public.h"

#define NFB	12
#define BS_SIZE	(4 << 20)	/* max access unit */

typedef VDEC_DRV_MRESULT_T (*create_t)(VAL_HANDLE_T *, VDEC_DRV_VIDEO_FORMAT_T);
typedef VDEC_DRV_MRESULT_T (*h_t)(VAL_HANDLE_T);
typedef VDEC_DRV_MRESULT_T (*init_t)(VAL_HANDLE_T, VDEC_DRV_RINGBUF_T *, VDEC_DRV_SEQINFO_T *);
typedef VDEC_DRV_MRESULT_T (*param_t)(VAL_HANDLE_T, int, void *, void *);
typedef VDEC_DRV_MRESULT_T (*dec_t)(VAL_HANDLE_T, VDEC_DRV_RINGBUF_T *, VDEC_DRV_FRAMEBUF_T *);
typedef VAL_RESULT_T (*mem_t)(VAL_MEMORY_T *, VAL_UINT32_T);
typedef VAL_RESULT_T (*cache_t)(VAL_MEMORY_T *, VAL_UINT32_T, VAL_UINT32_T);

static create_t cr; static h_t rel, deinit; static init_t init; static param_t getp; static dec_t dec;
static mem_t mem_alloc; static cache_t inval, flush;

/* the vendor FRAMEBUF_T is 176 bytes, the public header says 120: pad each one */
static struct slot { VDEC_DRV_FRAMEBUF_T f; unsigned char pad[136]; } sl[NFB];
static VAL_MEMORY_T fbm[NFB], bsm;
static int busy[NFB];
static VAL_HANDLE_T h;
static VDEC_DRV_RINGBUF_T ring;
static unsigned pw, ph, seqh, bw;	/* picture size, decoder buffer height, buffer width */
static unsigned char *outbuf;
static long frames;
static unsigned vformat = VDEC_DRV_VIDEO_FORMAT_H264;
static int outfd = 1;	/* frames go here; fd 1 is pointed at stderr because the vendor libs printf() */

static void *sym(void *lib, const char *n)
{
	void *p = dlsym(lib, n);
	if (!p) { fprintf(stderr, "vdpipe: missing %s\n", n); exit(1); }
	return p;
}

/* Put one unit of bitstream in the buffer. The vendor library trims trailing zero bytes by scanning backwards from the
 * END OF THE WHOLE BUFFER (rBase.u4Size), a byte at a time: with a 4 MB buffer and KB-sized frames that was ~60% of a core.
 * So advertise only this unit's size (512-aligned, zero padded). */
static void load_bs(const unsigned char *p, long len)
{
	long sz = (len + 511) & ~511L;
	memcpy(bsm.pvMemVa, p, len);
	memset((char *)bsm.pvMemVa + len, 0, sz - len);
	flush(&bsm, sizeof(bsm), 0);
	ring.rBase.u4Size = sz;
	ring.u4Read = (VAL_ULONG_T)bsm.pvMemVa;
	ring.u4Write = (VAL_ULONG_T)bsm.pvMemVa + len;
}

static int alloc(VAL_MEMORY_T *m, unsigned size)
{
	memset(m, 0, sizeof(*m));
	m->eMemType = VAL_MEM_TYPE_FOR_HW_CACHEABLE;
	m->u4MemSize = size;
	m->eAlignment = VAL_MEM_ALIGN_512;
	m->eMemCodec = VAL_MEM_CODEC_FOR_VDEC;
	return mem_alloc(m, sizeof(*m));
}

static void write_all(const void *p, size_t n)
{
	while (n) {
		ssize_t w = write(outfd, p, n);
		if (w < 0) { if (errno == EINTR) continue; exit(0); }	/* player went away */
		p = (const char *)p + w; n -= w;
	}
}

/* MTK block layout -> NV12: luma in 16x32 tiles, interleaved CbCr in 16x16 tiles, tiles row-major.
 * Chroma starts at bw * seqh (decoder buffer height, not the 64-aligned allocation). */
/* Copy tiled plane -> linear. A tile is th rows x 16 bytes stored contiguously; read each tile sequentially and
 * write its rows with 16-byte NEON stores (a memcpy call per 16-byte piece was most of vdpipe's CPU). */
static void untile(unsigned char *dst, const unsigned char *src, unsigned rows, unsigned th)
{
	unsigned tiles = bw / 16, full = pw / 16, tail = pw % 16;

	for (unsigned ty = 0; ty * th < rows; ty++) {
		unsigned nr = rows - ty * th < th ? rows - ty * th : th;
		unsigned char *d = dst + ty * th * pw;
		const unsigned char *t = src + ty * tiles * th * 16;
		for (unsigned tx = 0; tx < full; tx++, t += th * 16)
			for (unsigned r = 0; r < nr; r++)
				vst1q_u8(d + r * pw + tx * 16, vld1q_u8(t + r * 16));
		if (tail)
			for (unsigned r = 0; r < nr; r++)
				memcpy(d + r * pw + full * 16, t + r * 16, tail);
	}
}

/* MTK block layout -> NV12: luma in 16x32 tiles, interleaved CbCr in 16x16 tiles, tiles row-major.
 * Chroma starts at bw * seqh (decoder buffer height, not the 64-aligned allocation). */
static void emit(int i)
{
	const unsigned char *src = fbm[i].pvMemVa;

	inval(&fbm[i], sizeof(fbm[i]), 0);
	untile(outbuf, src, ph, 32);
	untile(outbuf + pw * ph, src + bw * seqh, ph / 2, 16);
	write_all(outbuf, pw * ph * 3 / 2);
	frames++;
}

static void drain(void)
{
	for (;;) {
		VDEC_DRV_FRAMEBUF_T *d = NULL;
		getp(h, VDEC_DRV_GET_TYPE_DISP_FRAME_BUFFER, NULL, &d);
		if (!d) break;
		int i = (int)(((char *)d - (char *)sl) / (long)sizeof(sl[0]));
		if (i >= 0 && i < NFB) emit(i);
	}
	for (;;) {
		VDEC_DRV_FRAMEBUF_T *d = NULL;
		getp(h, VDEC_DRV_GET_TYPE_FREE_FRAME_BUFFER, NULL, &d);
		if (!d) break;
		int i = (int)(((char *)d - (char *)sl) / (long)sizeof(sl[0]));
		if (i >= 0 && i < NFB) busy[i] = 0;
	}
}

static int setup(const unsigned char *hdr, long len)
{
	VDEC_DRV_SEQINFO_T seq;
	int r = cr(&h, vformat);
	if (r) { fprintf(stderr, "vdpipe: create failed %d\n", r); return -1; }
	load_bs(hdr, len);
	memset(&seq, 0, sizeof(seq));
	r = init(h, &ring, &seq);
	if (r) { fprintf(stderr, "vdpipe: init failed %d\n", r); return -1; }
	pw = seq.u4PicWidth & ~1u; ph = seq.u4PicHeight & ~1u; seqh = seq.u4Height;
	bw = (seq.u4Width + 15) & ~15u;
	unsigned bh = (seq.u4Height + 63) & ~63u, fbsize = bw * bh * 3 / 2 + 4096;
	for (int i = 0; i < NFB; i++) {
		if (alloc(&fbm[i], fbsize)) { fprintf(stderr, "vdpipe: frame buffer %d alloc failed\n", i); return -1; }
		memset(&sl[i], 0, sizeof(sl[i]));
		sl[i].f.rBaseAddr.u4VA = (VAL_ULONG_T)fbm[i].pvMemVa;
		sl[i].f.rBaseAddr.u4PA = (VAL_ULONG_T)fbm[i].pvMemPa;
		sl[i].f.rBaseAddr.u4Size = fbsize;
		sl[i].f.u4BufWidth = seq.u4Width;
		sl[i].f.u4BufHeight = seq.u4Height;
	}
	outbuf = malloc(pw * ph * 3 / 2);
	fprintf(stderr, "vdpipe: %ux%u (buffer %ux%u)\n", pw, ph, seq.u4Width, seq.u4Height);
	return 0;
}

static int decode(const unsigned char *au, long len)
{
	int k, r;
	VDEC_DRV_RINGBUF_T *rp = NULL;

	for (k = 0; k < NFB && busy[k]; k++) ;
	if (k == NFB) { fprintf(stderr, "vdpipe: no free frame buffer\n"); return -1; }
	if (au) {
		if (len > BS_SIZE - 512) { fprintf(stderr, "vdpipe: access unit too big (%ld), skipped\n", len); return 0; }
		load_bs(au, len);
		rp = &ring;
	}
	busy[k] = 1;
	r = dec(h, rp, &sl[k].f);
	if (r) { busy[k] = 0; fprintf(stderr, "vdpipe: decode -> %d\n", r); return r == VDEC_DRV_MRESULT_FATAL ? -1 : 0; }
	drain();
	return 0;
}

/* ---- Annex-B access unit splitter ---- */
static unsigned char *in; static long inlen, incap;
static long au_start, scan;		/* current AU begins at au_start; scan = next unparsed start code */
static int au_has_vcl, started;

static long next_sc(long p)
{
	for (; p + 3 <= inlen; p++)
		if (!in[p] && !in[p + 1] && in[p + 2] == 1) return p;
	return -1;
}

/* does the NAL at start code p begin a new access unit? */
static int starts_au(long p)
{
	int t = in[p + 3] & 0x1f;
	if (t == 9 || t == 7 || t == 8 || t == 6) return 1;
	if ((t == 1 || t == 5) && p + 4 < inlen) return (in[p + 4] & 0x80) != 0;	/* first_mb_in_slice == 0 */
	return 0;
}

/* handle the AU [au_start, end) */
static int au_done(long end)
{
	if (!started) {
		/* init with everything before the first slice of the first AU (SPS/PPS/SEI) */
		long p = au_start, q;
		while ((q = next_sc(p + 3)) >= 0 && q < end) {
			int t = in[q + 3] & 0x1f;
			if (t == 1 || t == 5) break;
			p = q;
		}
		long hdr = (q >= 0 && q < end) ? q : end;
		if (setup(in + au_start, hdr - au_start)) return -1;
		started = 1;
	}
	return decode(in + au_start, end - au_start);
}

/* IVF (ffmpeg -c:v copy -f ivf): 32-byte file header, then per frame u32le size + u64le pts + data.
 * Each frame packet goes to the decoder as one unit; the first one also initialises it. */
static int read_full(void *p, size_t n)
{
	while (n) {
		ssize_t r = read(0, p, n);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) return -1;
		p = (char *)p + r; n -= r;
	}
	return 0;
}

static int run_ivf(void)
{
	unsigned char hdr[32], fh[12];
	if (read_full(hdr, 32) || memcmp(hdr, "DKIF", 4)) { fprintf(stderr, "vdpipe: input is not IVF\n"); return 1; }
	in = malloc(BS_SIZE);
	while (!read_full(fh, 12)) {
		unsigned len = fh[0] | fh[1] << 8 | fh[2] << 16 | (unsigned)fh[3] << 24;
		if (len > BS_SIZE || read_full(in, len)) { fprintf(stderr, "vdpipe: bad IVF frame (%u bytes)\n", len); return 1; }
		if (!started) {
			if (setup(in, len)) return 1;
			started = 1;
		}
		if (decode(in, len)) return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int ivf = argc > 1 && !strcmp(argv[1], "vp9");
	if (ivf) vformat = VDEC_DRV_VIDEO_FORMAT_VP9;
	outfd = dup(1);
	dup2(2, 1);
	void *lib = dlopen("libvcodecdrv.so", RTLD_NOW), *ul = dlopen("libvcodec_utility.so", RTLD_NOW);
	if (!lib || !ul) { fprintf(stderr, "vdpipe: dlopen: %s\n", dlerror()); return 1; }
	cr = sym(lib, "eVDecDrvCreate"); rel = sym(lib, "eVDecDrvRelease"); deinit = sym(lib, "eVDecDrvDeInit");
	init = sym(lib, "eVDecDrvInit"); getp = sym(lib, "eVDecDrvGetParam"); dec = sym(lib, "eVDecDrvDecode");
	mem_alloc = sym(ul, "eVideoMemAlloc");
	inval = sym(ul, "eVideoInvalidateCache"); flush = sym(ul, "eVideoFlushCache");
	if (alloc(&bsm, BS_SIZE)) { fprintf(stderr, "vdpipe: bitstream alloc failed\n"); return 1; }
	ring.rBase.u4VA = (VAL_ULONG_T)bsm.pvMemVa;
	ring.rBase.u4PA = (VAL_ULONG_T)bsm.pvMemPa;
	ring.rBase.u4Size = bsm.u4MemSize;

	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	int eof = 0, err = 0;
	if (ivf) { err = run_ivf(); eof = 1; }
	else { incap = 8 << 20; in = malloc(incap); }
	au_start = -1; scan = 0;
	while (!eof && !err) {
		if (inlen == incap) {			/* compact: drop consumed bytes */
			long drop = au_start > 0 ? au_start : 0;
			if (!drop) { fprintf(stderr, "vdpipe: input AU larger than buffer\n"); break; }
			memmove(in, in + drop, inlen - drop); inlen -= drop; scan -= drop; au_start -= drop;
		}
		ssize_t n = read(0, in + inlen, incap - inlen);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) { eof = 1; break; }
		inlen += n;
		long p;
		while ((p = next_sc(scan)) >= 0 && p + 5 <= inlen) {
			if (au_start < 0) au_start = p;
			else if (starts_au(p) && au_has_vcl) {
				if (au_done(p)) { err = 1; break; }
				au_start = p; au_has_vcl = 0;
			}
			int t = in[p + 3] & 0x1f;
			if (t == 1 || t == 5) au_has_vcl = 1;
			scan = p + 3;
		}
	}
	if (!ivf && !err && au_start >= 0 && au_has_vcl) err = au_done(inlen) != 0;
	if (started) {
		while (!err && decode(NULL, 0) == 0) {	/* flush until the decoder stops returning frames */
			long before = frames; drain();
			if (frames == before) break;
		}
		deinit(h); rel(h);
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double sec = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
	fprintf(stderr, "vdpipe: %ld frames in %.2f s (%.1f fps)\n", frames, sec, frames / sec);
	return err;
}
