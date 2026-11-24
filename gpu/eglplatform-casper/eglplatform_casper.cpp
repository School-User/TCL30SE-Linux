// libhybris EGL platform "casper": window surfaces for Wayland clients on the PowerVR GE8320.
// The vendor EGL renders into gralloc1 buffers (CPU-cached, so the readback is fast); on every swap
// the buffer is locked, copied into a wl_shm buffer and attached to the app's wl_surface.
// Select with EGL_PLATFORM=casper. Only wl_egl_window native windows are presented; anything else renders unseen.
#include "eglnativewindowbase.h"
#include <hybris/common/dlfcn.h>
#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>
#include <wayland-client.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <list>
#include <time.h>
#include <vector>
extern "C" {
#include <ws.h>
#include <eglplatformcommon.h>
#include <sync/sync.h>
}

struct wl_egl_window_c {	/* wayland-egl-backend.h, version 3 */
	const intptr_t version;
	int width, height, dx, dy, attached_width, attached_height;
	void *driver_private;
	void (*resize_callback)(struct wl_egl_window_c *, void *);
	void (*destroy_window_callback)(void *);
	struct wl_surface *surface;
};

static bool dbg() { static int d = getenv("CASPER_EGL_DEBUG") ? 1 : 0; return d; }
#define LOG(...) do { if (dbg()) { fprintf(stderr, "[casper-egl] " __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* ---------------- gralloc1 (PowerVR module, opened directly: libhardware would pick gralloc.default) ---------------- */
static gralloc1_device_t *g1;
static GRALLOC1_PFN_CREATE_DESCRIPTOR g1_create;
static GRALLOC1_PFN_DESTROY_DESCRIPTOR g1_destroy;
static GRALLOC1_PFN_SET_DIMENSIONS g1_dim;
static GRALLOC1_PFN_SET_FORMAT g1_fmt;
static GRALLOC1_PFN_SET_PRODUCER_USAGE g1_prod;
static GRALLOC1_PFN_SET_CONSUMER_USAGE g1_cons;
static GRALLOC1_PFN_ALLOCATE g1_alloc;
static GRALLOC1_PFN_RELEASE g1_release;
static GRALLOC1_PFN_GET_STRIDE g1_stride;
static GRALLOC1_PFN_LOCK g1_lock;
static GRALLOC1_PFN_UNLOCK g1_unlock;

static bool g1_init()
{
	if (g1) return true;
	void *lib = hybris_dlopen("/vendor/lib64/hw/gralloc.rogue.so", 2);
	hw_module_t *m = lib ? (hw_module_t *)hybris_dlsym(lib, "HMI") : NULL;
	if (!m || m->methods->open(m, "gralloc", (hw_device_t **)&g1) || !g1) { g1 = NULL; return false; }
#define GF(v, T, id) v = (T)g1->getFunction(g1, id)
	GF(g1_create, GRALLOC1_PFN_CREATE_DESCRIPTOR, GRALLOC1_FUNCTION_CREATE_DESCRIPTOR);
	GF(g1_destroy, GRALLOC1_PFN_DESTROY_DESCRIPTOR, GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR);
	GF(g1_dim, GRALLOC1_PFN_SET_DIMENSIONS, GRALLOC1_FUNCTION_SET_DIMENSIONS);
	GF(g1_fmt, GRALLOC1_PFN_SET_FORMAT, GRALLOC1_FUNCTION_SET_FORMAT);
	GF(g1_prod, GRALLOC1_PFN_SET_PRODUCER_USAGE, GRALLOC1_FUNCTION_SET_PRODUCER_USAGE);
	GF(g1_cons, GRALLOC1_PFN_SET_CONSUMER_USAGE, GRALLOC1_FUNCTION_SET_CONSUMER_USAGE);
	GF(g1_alloc, GRALLOC1_PFN_ALLOCATE, GRALLOC1_FUNCTION_ALLOCATE);
	GF(g1_release, GRALLOC1_PFN_RELEASE, GRALLOC1_FUNCTION_RELEASE);
	GF(g1_stride, GRALLOC1_PFN_GET_STRIDE, GRALLOC1_FUNCTION_GET_STRIDE);
	GF(g1_lock, GRALLOC1_PFN_LOCK, GRALLOC1_FUNCTION_LOCK);
	GF(g1_unlock, GRALLOC1_PFN_UNLOCK, GRALLOC1_FUNCTION_UNLOCK);
	return g1_create && g1_alloc && g1_lock && g1_unlock && g1_stride;
}

class CasperBuffer : public BaseNativeWindowBuffer {
public:
	int status = -1;
	bool busy = false;
	CasperBuffer(unsigned w, unsigned h)
	{
		ANativeWindowBuffer::width = w;
		ANativeWindowBuffer::height = h;
		ANativeWindowBuffer::format = HAL_PIXEL_FORMAT_RGBA_8888;
		ANativeWindowBuffer::usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_OFTEN;
		gralloc1_buffer_descriptor_t d;
		g1_create(g1, &d);
		g1_dim(g1, d, w, h);
		g1_fmt(g1, d, HAL_PIXEL_FORMAT_RGBA_8888);
		g1_prod(g1, d, GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET | GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN);
		g1_cons(g1, d, GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE | GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN);
		buffer_handle_t hnd = 0;
		status = g1_alloc(g1, 1, &d, &hnd);
		if (g1_destroy) g1_destroy(g1, d);
		handle = hnd;
		uint32_t s = 0;
		if (!status) g1_stride(g1, hnd, &s);
		stride = s;
	}
	~CasperBuffer() { if (!status && g1_release) g1_release(g1, handle); }
};

/* Present XRGB8888, the standard format, not XBGR8888 (what the PowerVR gives us, R and B swapped while copying). Found 2026-11-23: sway's pixman
 * compositor only has fast blit/rotate paths for the standard formats; with XBGR8888 the landscape output rotation took ~100 ms per fullscreen
 * frame (8 fps), with XRGB8888 it keeps up with 60 fps. CASPER_XBGR=1 restores the old format. */
static bool want_xrgb() { static int v = -1; if (v < 0) v = getenv("CASPER_XBGR") ? 0 : 1; return v; }
static inline uint32_t swap_rb(uint32_t v) { return (v & 0xff00ff00u) | ((v & 0xffu) << 16) | ((v >> 16) & 0xffu); }

/* ---------------- wl_shm presentation ---------------- */
struct ShmBuf { wl_buffer *buf; bool busy; };

struct Presenter {
	wl_display *dpy = NULL;
	wl_event_queue *q = NULL;
	wl_shm *shm = NULL;
	wl_surface *surface = NULL;
	int w = 0, h = 0;
	void *map = NULL; size_t size = 0; int fd = -1;
	ShmBuf bufs[3] = {};
	int next = 0;
	bool ok() const { return shm && surface; }
};

static void reg_global(void *data, wl_registry *reg, uint32_t name, const char *iface, uint32_t ver)
{
	Presenter *p = (Presenter *)data;
	if (!strcmp(iface, "wl_shm")) p->shm = (wl_shm *)wl_registry_bind(reg, name, &wl_shm_interface, 1);
}
static void reg_remove(void *, wl_registry *, uint32_t) {}
static const wl_registry_listener reg_l = { reg_global, reg_remove };
static void buf_release(void *data, wl_buffer *) { ((ShmBuf *)data)->busy = false; }
static const wl_buffer_listener buf_l = { buf_release };

static void pres_open(Presenter *p, wl_display *d, wl_surface *s)
{
	p->dpy = d; p->surface = s;
	p->q = wl_display_create_queue(d);
	wl_display *wrap = (wl_display *)wl_proxy_create_wrapper(d);
	wl_proxy_set_queue((wl_proxy *)wrap, p->q);
	wl_registry *reg = wl_display_get_registry(wrap);
	wl_registry_add_listener(reg, &reg_l, p);
	wl_display_roundtrip_queue(d, p->q);
	wl_proxy_wrapper_destroy(wrap);
	LOG("presenter: wl_shm=%p", (void *)p->shm);
}

static void pres_size(Presenter *p, int w, int h)
{
	if (p->w == w && p->h == h && p->map) return;
	for (auto &b : p->bufs) { if (b.buf) wl_buffer_destroy(b.buf); b.buf = NULL; }
	if (p->map) munmap(p->map, p->size);
	if (p->fd >= 0) close(p->fd);
	p->w = w; p->h = h;
	size_t one = (size_t)w * h * 4;
	p->size = one * 3;
	p->fd = memfd_create("casper-egl", MFD_CLOEXEC);
	if (p->fd < 0 || ftruncate(p->fd, p->size)) { p->map = NULL; return; }
	p->map = mmap(NULL, p->size, PROT_READ | PROT_WRITE, MAP_SHARED, p->fd, 0);
	wl_shm_pool *pool = wl_shm_create_pool(p->shm, p->fd, p->size);
	for (int i = 0; i < 3; i++) {
		p->bufs[i].buf = wl_shm_pool_create_buffer(pool, i * one, w, h, w * 4, want_xrgb() ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_XBGR8888);
		p->bufs[i].busy = false;
		wl_buffer_add_listener(p->bufs[i].buf, &buf_l, &p->bufs[i]);
	}
	wl_shm_pool_destroy(pool);
	p->next = 0;
}

/* Wait (briefly) for the compositor to release an shm buffer. Dropping a frame is wrong for toolkits that pace on wl_surface.frame
 * callbacks (GTK): no commit means no callback until a long timeout. If nothing is released in time, the caller overwrites. */
static bool pres_ready(Presenter *p)
{
	if (!p->map) return true;
	for (int tries = 0; tries < 8; tries++) {
		wl_display_dispatch_queue_pending(p->dpy, p->q);
		for (auto &b : p->bufs) if (!b.busy) return true;
		if (wl_display_prepare_read_queue(p->dpy, p->q) != 0) continue;
		wl_display_flush(p->dpy);
		struct pollfd pfd = { wl_display_get_fd(p->dpy), POLLIN, 0 };
		if (poll(&pfd, 1, 8) > 0) wl_display_read_events(p->dpy); else wl_display_cancel_read(p->dpy);
	}
	wl_display_dispatch_queue_pending(p->dpy, p->q);
	for (auto &b : p->bufs) if (!b.busy) return true;
	return true;	/* still presenting: tear rather than stall */
}

static void pres_present(Presenter *p, const uint8_t *src, unsigned stride_px, int w, int h)
{
	wl_display_dispatch_queue_pending(p->dpy, p->q);
	pres_size(p, w, h);
	if (!p->map) return;
	int i = -1;
	for (int k = 0; k < 3; k++) if (!p->bufs[(p->next + k) % 3].busy) { i = (p->next + k) % 3; break; }
	if (i < 0) i = p->next;	/* all busy after the wait: reuse the next one (may tear) */
	p->next = (i + 1) % 3;
	uint8_t *dst = (uint8_t *)p->map + (size_t)i * w * h * 4;
	if (want_xrgb()) {
		for (int y = 0; y < h; y++) {
			uint32_t *d = (uint32_t *)(dst + (size_t)y * w * 4);
			const uint32_t *s2 = (const uint32_t *)src + (size_t)y * stride_px;
			for (int x = 0; x < w; x++) d[x] = swap_rb(s2[x]);
		}
	} else if (stride_px == (unsigned)w) memcpy(dst, src, (size_t)w * h * 4);
	else for (int y = 0; y < h; y++) memcpy(dst + (size_t)y * w * 4, src + (size_t)y * stride_px * 4, (size_t)w * 4);
	p->bufs[i].busy = true;
	wl_surface_attach(p->surface, p->bufs[i].buf, 0, 0);
	wl_surface_damage_buffer(p->surface, 0, 0, w, h);
	wl_surface_commit(p->surface);
	wl_display_flush(p->dpy);
}

/* ---------------- native window ---------------- */
static wl_display *g_native_display;

class CasperWindow : public EGLBaseNativeWindow {
public:
	CasperWindow(wl_egl_window_c *win) : m_win(win)
	{
		m_w = win ? win->width : 640;
		m_h = win ? win->height : 480;
		if (win && g_native_display && win->surface) pres_open(&m_pres, g_native_display, win->surface);
		pthread_mutex_init(&m_pmutex, NULL);
		if (win) {	/* libwayland-egl tells us when the wl_egl_window (and its surface) goes away */
			win->driver_private = this;
			win->destroy_window_callback = [](void *priv) { ((CasperWindow *)priv)->detach(); };
		}
		LOG("window %dx%d presenting=%d", m_w, m_h, m_pres.ok());
		pthread_mutex_init(&m_mutex, NULL);
		pthread_cond_init(&m_cond, NULL);
	}
	~CasperWindow() { stopWorker(); if (m_win) { m_win->destroy_window_callback = NULL; m_win->driver_private = NULL; } destroyBuffers(); }
	int setSwapInterval(int) { return 0; }
	void detach()
	{
		pthread_mutex_lock(&m_pmutex);
		m_win = NULL;
		m_pres.surface = NULL;
		pthread_mutex_unlock(&m_pmutex);
	}
protected:
	int dequeueBuffer(BaseNativeWindowBuffer **buffer, int *fenceFd)
	{
		if (statsOn() && m_td0.size() < 20000) m_td0.push_back(nowns());
		{ int pw = m_win ? m_win->width : m_w, ph = m_win ? m_win->height : m_h;
		  if (!m_bufs.empty() && (pw != m_w || ph != m_h)) drain(); }	/* the worker may still hold buffers that allocate() frees */
		pthread_mutex_lock(&m_mutex);
		int cw = m_win ? m_win->width : m_w, ch = m_win ? m_win->height : m_h;
		if (m_bufs.empty() || cw != m_w || ch != m_h) { m_w = cw; m_h = ch; allocate(); }
		CasperBuffer *b = NULL;
		for (;;) {
			for (auto *c : m_bufs) if (!c->busy) { b = c; break; }
			if (b) break;
			pthread_cond_wait(&m_cond, &m_mutex);
		}
		b->busy = true;
		*buffer = b; *fenceFd = -1;
		pthread_mutex_unlock(&m_mutex);
		if (statsOn() && m_td1.size() < 20000) m_td1.push_back(nowns());
		return 0;
	}
	int queueBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
	{
		CasperBuffer *b = (CasperBuffer *)buffer;
		if (statsOn() && m_tq.size() < 20000) m_tq.push_back(nowns());
		static int qn; if (qn < 8 || qn % 300 == 0) LOG("queueBuffer #%d fence=%d %ux%d stride=%d pres=%d", qn, fenceFd, b->width, b->height, b->stride, m_pres.ok()); qn++;
		if (!m_pres.ok()) { if (fenceFd >= 0) { sync_wait(fenceFd, -1); close(fenceFd); } release(b); return 0; }
		/* Present on a worker thread: the fence wait, the CPU copy and the commit (about as long as the GPU render) no longer
		 * stall the app's render thread, so the GPU draws frame N+1 while frame N is copied. At most one frame waits behind the one
		 * being presented; if the app outruns us the oldest waiting frame is dropped (latest frame wins). */
		static const bool forceSync = getenv("CASPER_SYNC") != NULL;	/* debugging: the old synchronous present */
		if (forceSync) { present(b, fenceFd); release(b); return 0; }
		pthread_mutex_lock(&m_qm);
		if (!m_thOn) { m_stop = false; if (!pthread_create(&m_th, NULL, workerMain, this)) m_thOn = true; }
		if (!m_thOn) { pthread_mutex_unlock(&m_qm); present(b, fenceFd); release(b); return 0; }	/* no thread: old synchronous behaviour */
		while (m_q.size() >= 2) {
			Job o = m_q.front(); m_q.pop_front();
			if (o.fence >= 0) { sync_wait(o.fence, -1); close(o.fence); }	/* the GPU must finish before the buffer is reused */
			release(o.b); m_pending--;
		}
		m_q.push_back(Job{ b, fenceFd }); m_pending++;
		pthread_cond_signal(&m_qc);
		pthread_mutex_unlock(&m_qm);
		return 0;
	}
	int cancelBuffer(BaseNativeWindowBuffer *buffer, int fenceFd)
	{
		if (fenceFd >= 0) close(fenceFd);
		release((CasperBuffer *)buffer);
		return 0;
	}
	int lockBuffer(BaseNativeWindowBuffer *) { return 0; }
	unsigned type() const { return NATIVE_WINDOW_SURFACE; }
	unsigned width() const { return m_w; }
	unsigned height() const { return m_h; }
	unsigned format() const { return HAL_PIXEL_FORMAT_RGBA_8888; }
	unsigned defaultWidth() const { return m_w; }
	unsigned defaultHeight() const { return m_h; }
	unsigned queueLength() const { return 1; }
	unsigned transformHint() const { return 0; }
	unsigned getUsage() const { return GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_SW_READ_OFTEN; }
	int setUsage(uint64_t) { return 0; }
	int setBuffersFormat(int) { return 0; }
	int setBuffersDimensions(int, int) { return 0; }
	int setBufferCount(int) { return 0; }
private:
	struct Job { CasperBuffer *b; int fence; };
	std::vector<long long> m_tq, m_tc, m_td0, m_td1;	/* CASPER_STATS=1: queueBuffer() and commit times (ns), written to /tmp/casper-stats.<pid>.txt at exit */
	static long long nowns() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
	static bool statsOn() { static int on = -1; if (on < 0) on = getenv("CASPER_STATS") ? 1 : 0; return on; }
	void dumpStats()
	{
		if (!statsOn() || m_tq.size() < 10) return;
		char path[64]; snprintf(path, sizeof path, "/tmp/casper-stats.%d.txt", (int)getpid());
		FILE *f = fopen(path, "w"); if (!f) return;
		fprintf(f, "queue"); for (auto t : m_tq) fprintf(f, " %lld", t); fprintf(f, "\ncommit"); for (auto t : m_tc) fprintf(f, " %lld", t); fprintf(f, "\ndeq0"); for (auto t : m_td0) fprintf(f, " %lld", t); fprintf(f, "\ndeq1"); for (auto t : m_td1) fprintf(f, " %lld", t); fprintf(f, "\n"); fclose(f);
	}
	static void *workerMain(void *a) { ((CasperWindow *)a)->work(); return NULL; }
	void work()
	{
		for (;;) {
			pthread_mutex_lock(&m_qm);
			while (m_q.empty() && !m_stop) pthread_cond_wait(&m_qc, &m_qm);
			if (m_q.empty()) { pthread_mutex_unlock(&m_qm); return; }
			Job j = m_q.front(); m_q.pop_front();
			pthread_mutex_unlock(&m_qm);
			present(j.b, j.fence);
			release(j.b);
			pthread_mutex_lock(&m_qm);
			if (--m_pending == 0) pthread_cond_broadcast(&m_qdone);
			pthread_mutex_unlock(&m_qm);
		}
	}
	/* wait for the GPU, copy the frame into a wl_shm buffer and commit it */
	void present(CasperBuffer *b, int fenceFd)
	{
		if (fenceFd >= 0) { sync_wait(fenceFd, -1); close(fenceFd); }
		pthread_mutex_lock(&m_pmutex);
		if (m_pres.ok() && pres_ready(&m_pres)) {
			/* the PowerVR gralloc1 lock writes more than one pointer through outData (it clobbered the next local): give it room */
			void *vas[8] = { 0 };
			void *&va = vas[0];
			gralloc1_rect_t r = { 0, 0, (int32_t)b->width, (int32_t)b->height };
			if (!g1_lock(g1, b->handle, 0, GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN, &r, vas, -1) && va) {
				pres_present(&m_pres, (const uint8_t *)va, b->stride, b->width, b->height);
				if (statsOn() && m_tc.size() < 20000) m_tc.push_back(nowns());
				int f = -1; g1_unlock(g1, b->handle, &f); if (f >= 0) close(f);
			}
		}
		pthread_mutex_unlock(&m_pmutex);
	}
	void drain() { pthread_mutex_lock(&m_qm); while (m_pending) pthread_cond_wait(&m_qdone, &m_qm); pthread_mutex_unlock(&m_qm); }
	void stopWorker()
	{
		if (!m_thOn) return;
		pthread_mutex_lock(&m_qm); m_stop = true; pthread_cond_signal(&m_qc); pthread_mutex_unlock(&m_qm);
		pthread_join(m_th, NULL); m_thOn = false;
		dumpStats();
	}
	void release(CasperBuffer *b)
	{
		pthread_mutex_lock(&m_mutex);
		b->busy = false;
		pthread_cond_signal(&m_cond);
		pthread_mutex_unlock(&m_mutex);
	}
	void allocate()
	{
		destroyBuffers();
		for (int i = 0; i < 3; i++) {
			CasperBuffer *b = new CasperBuffer(m_w, m_h);
			b->common.incRef(&b->common);
			if (b->status) { LOG("gralloc allocate failed %d", b->status); b->common.decRef(&b->common); break; }
			m_bufs.push_back(b);
		}
		LOG("allocated %zu buffers %dx%d", m_bufs.size(), m_w, m_h);
	}
	void destroyBuffers()
	{
		for (auto *b : m_bufs) b->common.decRef(&b->common);
		m_bufs.clear();
	}
	wl_egl_window_c *m_win;
	int m_w, m_h;
	Presenter m_pres;
	std::list<CasperBuffer *> m_bufs;
	pthread_mutex_t m_mutex, m_pmutex;
	pthread_cond_t m_cond;
	std::list<Job> m_q;
	pthread_mutex_t m_qm = PTHREAD_MUTEX_INITIALIZER;
	pthread_cond_t m_qc = PTHREAD_COND_INITIALIZER, m_qdone = PTHREAD_COND_INITIALIZER;
	pthread_t m_th; bool m_thOn = false, m_stop = false; int m_pending = 0;
};


static bool looks_like_wl_egl_window(EGLNativeWindowType w)
{
	if (!w) return false;
	wl_egl_window_c *e = (wl_egl_window_c *)w;
	return e->version >= 1 && e->version <= 16 && e->width > 0 && e->width < 16384 && e->height > 0 && e->height < 16384;
}

extern "C" void casperws_init_module(struct ws_egl_interface *egl_iface)
{
	if (!g1_init()) fprintf(stderr, "casper-egl: cannot open PowerVR gralloc\n");
	eglplatformcommon_init(egl_iface);
}

extern "C" _EGLDisplay *casperws_GetDisplay(EGLNativeDisplayType display)
{
	if (display != EGL_DEFAULT_DISPLAY) g_native_display = (wl_display *)display;
	return new _EGLDisplay;
}

extern "C" void casperws_releaseDisplay(_EGLDisplay *dpy) { delete dpy; }

extern "C" EGLNativeWindowType casperws_CreateWindow(EGLNativeWindowType win, _EGLDisplay *)
{
	CasperWindow *w = new CasperWindow(looks_like_wl_egl_window(win) ? (wl_egl_window_c *)win : NULL);
	w->common.incRef(&w->common);
	return (EGLNativeWindowType) static_cast<struct ANativeWindow *>(w);
}

extern "C" void casperws_DestroyWindow(EGLNativeWindowType win)
{
	CasperWindow *w = static_cast<CasperWindow *>((struct ANativeWindow *)win);
	LOG("DestroyWindow %p", (void *)w);
	if (!getenv("CASPER_EGL_LEAK")) w->common.decRef(&w->common);
}

/* GTK4/libepoxy insist on EGL_KHR_display_reference + eglQueryDisplayAttrib*; the vendor EGL has neither. Advertise it and fail the query. */
static EGLBoolean casper_query_display_attrib(EGLDisplay, EGLint, EGLAttrib *value) { if (value) *value = 0; return EGL_FALSE; }
static const char *casper_query_device_string(void *, EGLint) { return NULL; }
static EGLBoolean casper_query_devices(EGLint, void **, EGLint *n) { if (n) *n = 0; return EGL_FALSE; }
static EGLBoolean casper_query_device_attrib(void *, EGLint, EGLAttrib *v) { if (v) *v = 0; return EGL_FALSE; }

extern "C" __eglMustCastToProperFunctionPointerType casperws_eglGetProcAddress(const char *n)
{
	if (n && (!strcmp(n, "eglQueryDisplayAttribEXT") || !strcmp(n, "eglQueryDisplayAttribKHR")))
		return (__eglMustCastToProperFunctionPointerType)casper_query_display_attrib;
	if (n && !strcmp(n, "eglQueryDeviceStringEXT")) return (__eglMustCastToProperFunctionPointerType)casper_query_device_string;
	if (n && !strcmp(n, "eglQueryDevicesEXT")) return (__eglMustCastToProperFunctionPointerType)casper_query_devices;
	if (n && !strcmp(n, "eglQueryDeviceAttribEXT")) return (__eglMustCastToProperFunctionPointerType)casper_query_device_attrib;
	return eglplatformcommon_eglGetProcAddress(n);
}

extern "C" const char *casperws_eglQueryString(EGLDisplay dpy, EGLint name, const char *(*real)(EGLDisplay, EGLint))
{
	const char *base = eglplatformcommon_eglQueryString(dpy, name, real);
	if (name != EGL_EXTENSIONS || !base) return base;
	static char buf[4096];
	if (strstr(base, "EGL_KHR_display_reference")) return base;
	snprintf(buf, sizeof buf, "%s EGL_KHR_display_reference EGL_EXT_device_base EGL_EXT_device_query", base);
	return buf;
}
extern "C" void casperws_passthroughImageKHR(EGLContext *c, EGLenum *t, EGLClientBuffer *b, const EGLint **a) { eglplatformcommon_passthroughImageKHR(c, t, b, a); }
extern "C" void casperws_setSwapInterval(EGLDisplay, EGLNativeWindowType win, EGLint i)
{
	static_cast<CasperWindow *>((struct ANativeWindow *)win)->setSwapInterval(i);
}

struct ws_module ws_module_info = {
	casperws_init_module, casperws_GetDisplay, NULL, casperws_CreateWindow, casperws_DestroyWindow,
	casperws_eglGetProcAddress, casperws_passthroughImageKHR, casperws_eglQueryString,
	NULL, NULL, casperws_setSwapInterval, casperws_releaseDisplay,
};
