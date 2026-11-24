# Getting the PowerVR GPU working on a mainline-Linux phone

**Device:** TCL 30 SE (MediaTek MT6765/MT6762, 4 GB RAM, 720x1600)
**OS:** Debian 13 on a mainline-fork kernel 7.1, sway plus a custom Rust/iced home shell ("pocketshell")
**GPU:** PowerVR Rogue GE8320 (BVNC 22.87.104.18, MIPS firmware processor)
**Date:** 2026-11-12 to 2026-11-13
**Rule of the project:** everything runs natively on Linux, with no Android system underneath.

## Result

The GPU runs real work on a mainline Linux phone, and the stack starts by itself at boot.

- The kernel driver loads and the GPU firmware boots.
- The phone's original Android Vulkan driver runs on Linux and sends commands to the GPU.
- `vulkaninfo` from a normal Linux (glibc) process lists "PowerVR Rogue GE8320", Vulkan 1.1.131.
- A spinning-triangle demo renders on the GPU and reaches about 141 fps on the screen.

Not working yet: the desktop and apps still draw on the CPU, and OpenGL/EGL crashes. See "Open problems".

## Why this was hard

The GE8320 has no open-source driver. Mesa has no PowerVR Rogue support for this generation. The vendor ships a kernel module plus closed userspace libraries, built for Android's kernel 4.x and for Android's userspace, which is a different world from glibc Linux. That gave three problems:

1. A kernel driver for the GPU that builds and runs on kernel 7.1.
2. Board glue for this SoC: power, clocks, interrupts, memory.
3. A way to run the vendor's userspace libraries without Android.

## 1. Kernel driver

- **Source:** Texas Instruments' open PowerVR DDK, version 1.13@5776728. It has the same release number as the phone's stock driver and includes this GPU core's definition.
- **Port:** it was built against our 7.1 tree. The first trial build had about 137 errors in OS glue (dma_resv, trace macros, `strlcpy`, the display driver). I fixed the first two groups, `strlcpy` was renamed, and I dropped the display driver. Buffer sync is switched off.
- **Platform layer:** I wrote a new system layer, `rgx_mt6765`, using the MediaTek glue from a vivo kernel source for the same SoC as reference. Register base `0x13000000`, interrupt SPI 259, clock gate at `0x13ffe000`. The bootloader (LK) leaves the GPU power domains, clock and BG3D block powered, so the module could read the GPU's ID straight away.
- **Memory:** the GPU can only address 32 bits of DMA, so the mask was set to 32-bit. The GPU is not cache coherent with the CPU, so CPU mappings of GPU memory must honour page protections.

### The firmware boot bug

The firmware (a small MIPS CPU inside the GPU) timed out at boot. The cause: the driver mapped the firmware buffers with `vm_map_ram`, which ignores page protection and gives a cached mapping. The CPU wrote the firmware image into its cache while the GPU read stale memory. Switching to `vmap` with the correct page protection fixed it, and the firmware reported status OK. The fix is in `physmem_osmem_linux.c`.

### Device tree

Probing the GPU's device-tree node with a power domain fails with error -110. As a workaround the module `casper-gpu-dt.ko` creates the node at runtime (clock handles from topckgen and apmixed, no power domain), then `pvrsrvkm.ko` loads. Fixing the real DT probe is still open.

## 2. Android userspace on mainline Linux

The vendor libraries expect Android services. Android's binaries are ordinary Linux ELF files, so a chroot at `/srv/android` runs them under Android's own linker (`linker64`). What was needed:

- **Binder:** enabled in the kernel (binderfs, devices created with `BINDER_CTL_ADD`). A small kernel patch in `binder.c` returns a fallback security context, because mainline has no SELinux context for Android's service registration.
- **Service managers:** `servicemanager` and `hwservicemanager`, with a stub `libselinux` (right symbol version plus a few extra symbols).
- **Property system:** a shim implementing property lookup, callbacks, serial and wait. Without it, `WaitForProperty` spun forever.
- **ION:** `casper-ion.ko`, a legacy ION allocator exporting dma-buf with 32-bit-addressable pages, because mainline no longer has `/dev/ion`.
- **Graphics allocator HAL:** `android.hardware.graphics.allocator@4.0` starts and registers.

With these, the vendor Vulkan HAL ran a GPU command natively.

## 3. Using the GPU from a normal Linux program

A glibc program cannot load Android libraries directly. **libhybris** emulates Android's linker inside a glibc process. I patched `hybris/common/hooks.c`, for example to hook `__ctype_get_mb_cur_max` and make `__cxa_atexit` a no-op, and wrote a small Khronos Vulkan ICD (`libhybris_vk_icd.so`) that forwards to the vendor HAL. The ICD is linked with `-z nodelete` because the Vulkan loader loaded it twice and re-initialised the linker. To satisfy tools that look for a DRM device, I added `VK_EXT_physical_device_drm` through the ICD.

Present path: render offscreen, read the image back, and copy it to `/dev/fb0` (simpledrm, 720x1600, stride 2880). Readback from a host-cached memory type is what makes 141 fps possible.

## Boot integration

`casper-gpu.service` runs `/usr/local/bin/casper-gpu-start`, which loads the three modules, mounts binderfs and starts the Android services. It has a hang guard: a marker file `/var/lib/casper-gpu.trying` exists while starting and is removed on success. If a boot died mid-start, the next boot skips the GPU.

A first boot test failed with a systemd ordering loop (`casper-dvfs` runs after `multi-user.target`), so the GPU and audio units are wanted by `graphical.target`. The audio units had the same older loop. After the change, a clean reboot brought the whole stack up.

## Open problems

- **OpenGL/EGL:** the vendor library `libIMGegl` crashes in `eglChooseConfig` because a config table is NULL. The Mesa Zink route (OpenGL on top of Vulkan) is blocked: this Vulkan driver has no timeline semaphores and no geometry shader.
- **No real display path:** there is no swapchain or WSI. Frames reach the screen by a CPU copy.
- **Desktop not accelerated:** pocketshell and apps still use CPU rendering. Next step: a Vulkan-native shell.
- **Smaller items:** device-tree power-domain probe (-110), GPU frequency scaling and thermals, dma_resv/buffer-sync support, a `/proc/ged` shim.

## Repository map (`~/Projects/tcl30se-gsi/gpu`)

| Path | What |
|---|---|
| `ti-src/ti113`, branch `casper-7.1` | Ported kernel driver, platform layer `rgx_mt6765`; build with `ti-src/build-mt6765.sh` |
| `../debian/src/casper-gpu-dt`, `casper-ion` | Runtime DT overlay and ION shim modules |
| `android-up.sh`, `boot/` | Stack start script and systemd units |
| `propshim.c`, `selinux_stub.c` | Android property and libselinux shims |
| `libhybris/`, `icd/` | Patched libhybris and the Vulkan ICD |
| `test_vk*.c`, `tri.png` | Vulkan test programs and triangle output |
| `FINDINGS.md` | Full working notes |

## Update 2026-11-21: the desktop shell now runs on the GPU

- **pocketshell renders on the PowerVR GPU.** `~/Projects/pocketshell` uses iced's wgpu renderer with a patched `vendor/iced_wgpu`
  (`src/window/compositor.rs`): since the vendor Vulkan driver has no swapchain, every frame is rendered into an offscreen
  `Bgra8Unorm` texture on the GPU, read back and written into the softbuffer (wl_shm) window. iced feature `web-colors` is on so
  blending matches the tiny-skia renderer (linear-light blending made translucent layers visibly brighter).
- **Measured** (`POCKETSHELL_BENCH=1`, panel flipped as fast as the renderer allows): CPU build 10-16 frames/s, GPU build 78-85 frames/s.
  Idle: 0.15% CPU. Cost: ~145 MB RSS vs ~25 MB. Screenshots (grim) of both builds differ by 1.5/255 mean (text anti-aliasing).
- **Kernel bug found and fixed:** the first full-size readback oopsed the kernel in `pvrsrvkm` `FlushRange` (`dc ivac` on a read-only user
  mapping needs write permission). `services/server/env/linux/osfunc_arm64.c` now uses `dc civac` for INVALIDATE.
  Originals: `ti-src/pvrsrvkm.ko.orig-2026-11-21`, `ti-src/osfunc_arm64.c.orig-2026-11-21`.
  Rebuild needs compiler wrappers in /tmp/claude-1000/shim (aarch64-linux-gnu-{gcc,g++,ld,ar,nm,objcopy,...} -> clang/llvm), then the
  `make kbuild` line of `ti-src/build-mt6765.sh`.
- **Permissions:** `/dev/pvr_sync` was root-only (siddh got Device(Lost)); `/root/android-up.sh` now chmods it 666 at boot.
- **Launcher:** `/usr/local/bin/pocketshell-run` prefers `/usr/local/bin/pocketshell-gpu`, falls back to `/usr/local/bin/pocketshell` (CPU)
  if casper-gpu is not active, `/dev/pvr_sync` is not writable, or the GPU build dies within 5 s (writes `~/.config/pocketshell/gpu-disabled`;
  `rm` it to retry). Old launcher: `pocketshell-run.bak-2026-11-21`. NOT yet verified across a reboot (boot-time race: it waits up to 30 s for casper-gpu).
- **Still CPU:** every other app (foot, Firefox, ...) and sway's compositing. Vendor EGL/GLES still crashes in `libIMGegl`; Zink is blocked.
  Next candidates: ANGLE for GL apps, or reverse the `IMGeglChooseConfig` NULL table.

## Update 2026-11-21 (later): tuning the GPU path

Per-frame cost with POCKETSHELL_PROFILE=1 (printed by the patched compositor): render_submit (iced CPU work) ~5.3 ms, GPU draw + readback ~4-5.6 ms,
copy to the wl_shm window ~0.7 ms. 
- **CPU cluster matters most:** pinned to cpu0-3 (fast, ~1.86 GHz) the shell presents 76 frames/s (bench ~124); on cpu4-7 61 frames/s (bench ~90);
  unpinned it wanders between them. `pocketshell-run` now starts the shell with `taskset -c 0-3`.
- **GPU clock sweep** (MFGPLL = pcw/2^14 * 26 MHz / 4, CON1 @ 0x1000C250, bit 31 latches the new value; `gputune.sh`): GPU wait+readback
  230 MHz 12.8 ms, 345 MHz 7.4, 460 MHz (LK default) 5.6, 520 MHz 4.9, 580 MHz 4.2, 640 MHz 4.0 (flat above ~580). No GPU errors up to 640.
  Chosen: 520 MHz, via the driver's `casper,mfgpll-hz` DT property (set in debian/src/casper-gpu-dt/gpu.dtso, so the firmware's timing value matches).
  New casper-gpu-dt.ko is staged at /root/casper-gpu-dt.ko on the phone (orig: casper-gpu-dt.ko.orig-2026-11-21); it takes effect at the NEXT BOOT
  (module is loaded by /root/android-up.sh). GPU shares the VCORE rail, so no voltage change was possible; going past 580 MHz buys nothing anyway.

## Update 2026-11-21 (night): vendor EGL / OpenGL ES 3.2 WORKS (the "libIMGegl crash" was a missing symlink)

- **Root cause:** stock Android has `/system/vendor -> /vendor`. Our chroot (/srv/android) did not, so libIMGegl could not dlopen
  `/system/vendor/lib64/egl/libGLESv1_CM_mtk.so` / `libGLESv2_mtk.so`. With no GLES client libs the driver's two capability flags
  (global state +0x1a18 / +0x1a1c) stayed 0, `FUN_0011b704` (config builder) passed a NULL base list to `FUN_001197d0` (attribute-list append,
  no NULL check) and `eglGetConfigs` segfaulted at address 0x10.
- **Fix:** `ln -s /vendor /srv/android/system/vendor` (now also done idempotently in `/root/android-up.sh`).
- **How it was found:** strace of the bionic test (`strace -f -e trace=openat,... chroot /srv/android /data/run /data/test_egl`) showed the two ENOENT
  opens; Ghidra (REA) decompile of libIMGegl: `IMGeglInitialize 0x12453c` -> platform table 0x14f388[+0x10] = `WSEGL_InitialiseDisplay 0x1491f8`
  (asks gralloc.rogue for buffer formats via get_function ids 0x3ea/0x450/0x44d; verified healthy with `test_gralloc.c`, 34 formats) ->
  `eglGetConfigs 0x1282f4` loops `FUN_0011b704`. (The earlier theories - gralloc formats, /proc/ged, configstore - were NOT the cause.)
- **Result:** bionic (`/data/test_egl`) and glibc via libhybris as user siddh (`/usr/local/bin/test_egl_hybris`, built from gpu/test_egl_hybris.c with
  `gcc ... -L/usr/local/hybris/lib -lhybris-common -ldl -lpthread`): EGL 1.4, 67 configs, pbuffer + context OK, `GL_RENDERER = PowerVR Rogue GE8320`,
  `GL_VERSION = OpenGL ES 3.2 build 1.13@5776728`, glClear+glReadPixels correct, no kernel oops.
- **Still missing for "apps use the GPU":** libhybris's EGL/GLESv2 wrapper libs (only common/properties/hardware are installed) so ordinary glibc programs
  can link libEGL/libGLESv2; a window path (no WSI: render to pbuffer + readback to wl_shm like pocketshell, or hybris wayland-egl which needs compositor
  support); then SDL/GZDoom/Firefox-style apps. `/proc/ged` is still absent (harmless GED log spam).

## Update 2026-11-21 (night 2): ordinary glibc EGL/GLES2 programs run on the GPU (`gpurun`)

- Built and installed the rest of libhybris on the phone (sources /root/build/libhybris/hybris): libsync, ui, gralloc, platforms (common), egl (+ null/fbdev/hwcomposer
  platforms), glesv1, glesv2 -> /usr/local/hybris/lib (libEGL.so.1, libGLESv2.so.2, libGLESv1_CM.so.1). gcc needed bionic annotations neutralised:
  `make CPPFLAGS="-include /root/build/hdr/bionic_compat.h"` (gpu/bionic_compat.h: __INTRODUCED_IN, _Nullable/_Nonnull, ...); libsync must be built before platforms/egl.
- **Gotcha:** libhybris's libEGL loads Android's framework `libEGL.so`, which calls SurfaceFlinger props -> `android.hardware.configstore@1.0` (HIDL service,
  absent on this system) and spins forever in libhidlbase. Found with gdb + a frame-pointer walk script over /proc/PID/maps (gpu: fpwalk.py idea).
  Workaround: libhybris honours `LIBEGL` / `LIBGLESV2`, so point them at the vendor driver: `LIBEGL=/vendor/lib64/egl/libEGL_mtk.so LIBGLESV2=/vendor/lib64/egl/libGLESv2_mtk.so`.
- `/usr/local/bin/gpurun <cmd>` sets LD_LIBRARY_PATH=/usr/local/hybris/lib, HYBRIS_LD_LIBRARY_PATH, EGL_PLATFORM=null and the two vars above.
  Verified as user siddh: `gpurun test_egl_std` (gpu/test_egl_std.c, plain `-lEGL -lGLESv2`, no hybris API): EGL 1.4, GL_RENDERER PowerVR Rogue GE8320,
  OpenGL ES 3.2, glReadPixels correct, no kernel oops.
- **Limits:** EGL_PLATFORM=null means pbuffer/offscreen only; no native window surfaces. Next: a window path (render to pbuffer/FBO + glReadPixels -> wl_shm,
  e.g. via an SDL/EGL shim, or hybris wayland platform + compositor support), then SDL2 (GZDoom etc.).

## Update 2026-11-21 (evening): why GL readback is slow, and the ashmem + gralloc work

- **Measured (`bench_gl.c`, GLES via hybris, pinned to cpu0-3):** the GPU draws fast (640x360, 4 overdraw layers: 741 fps; 720x1600: 217 fps)
  but `glReadPixels` collapses it to 109 / 24 fps. PBOs do not help. Split timing (`bench_split`): GPU copy to the PBO is cheap
  (0.65 ms / 1.44 ms) but the CPU `memcpy` out of the mapped buffer takes 7.2 ms / 35.7 ms (~130 MB/s): the GLES driver maps its buffers uncached.
  The Vulkan path (wgpu) maps host-visible memory cached, which is why the shell readback is ~7x faster.
- **Plan:** render into a gralloc buffer allocated with HW_RENDER|SW_READ_OFTEN (CPU-cached), via EGLImage + FBO, then `hybris_gralloc_lock` + memcpy.
- **Blocker 1 (solved): no /dev/ashmem.** Kernel has memfd but no ashmem; Android 12 libcutils only uses memfd if properties (ro.vndk.version,
  sys.use_memfd) are readable, and libhybris loads the real bionic libc for these libs so its property hooks are bypassed.
  Wrote `debian/src/casper-ashmem/casper-ashmem.c` (minimal ashmem on shmem; pin/unpin no-ops, ABI-compatible ioctls). Loaded by android-up.sh.
  `hybris_gralloc_allocate` now returns 0 (stride 640 for 640 wide RGBA).
- **Blocker 2 (solved): hybris eglCreateImageKHR null function pointer.** Vendor libEGL exposes the image extensions only through eglGetProcAddress,
  not dlsym. Patched libhybris `egl/egl.c` (helper `casper_ext`) - copy in `gpu/hybris-patches/egl.c`. Also patched `common/hooks.c` property
  hooks (`__system_property_find/read/read_callback`); harmless but not the fix. Originals backed up on the phone as *.orig-2026-11-21.
- **Blocker 3 (solved): wrong gralloc module.** `ValidateNativeHandle: Unexpected native handle size - got 9 expected 50` (visible by strace -e writev).
  hybris_gralloc_allocate goes through Android libhardware, whose property lookups hit the real bionic libc (no /dev/__properties__), so it picks
  AOSP's generic `gralloc.default.so` (small ashmem handles) instead of PowerVR's `gralloc.rogue.so`. Fix: bypass it - `hybris_dlopen("/vendor/lib64/hw/gralloc.rogue.so")`,
  `HMI->methods->open("gralloc")` gives a **gralloc1** device (getFunction: create descriptor / set dims,format,producer+consumer usage / allocate / get stride / lock / unlock).
  Buffer allocated with producer GPU_RENDER_TARGET|CPU_READ_OFTEN is accepted by eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID, no attributes), FBO via glEGLImageTargetTexture2DOES is complete.
- **Result (`gpu/bench_gralloc_rb.cpp`, 4 overdraw layers + lock + memcpy):** 640x360 391 fps (was 109); 720x1600 127 fps (was 24; CPU copy 2.8 ms vs 35.7 ms); 480x270 567 fps. Works as siddh.
- **Rebooted, verified:** casper-ashmem loads at boot (android-up.sh), casper-charge keeps REG04=0x1f.
- **Next:** make this a reusable window path - an EGL surface shim (eglCreateWindowSurface -> gralloc1 buffer FBO, eglSwapBuffers -> lock + copy to wl_shm) so SDL/GL apps can run.

## Update 2026-11-21 (night): GL window path WORKS - SDL2 apps and Doom run on the GPU

- **`eglplatform_casper.so`** (source `gpu/eglplatform-casper/eglplatform_casper.cpp`, installed in `/usr/local/hybris/lib/libhybris/`): a libhybris EGL platform
  ("EGL_PLATFORM=casper"). `CreateWindow` takes the app's `wl_egl_window` (SDL passes the wl_display via eglGetDisplay), returns a custom ANativeWindow that
  hands the vendor EGL three gralloc1 buffers (PowerVR module opened directly, CPU-cached). On every swap: sync_wait fence, gralloc1 lock, copy into one of three wl_shm
  buffers (XBGR8888), attach/damage/commit on the app's wl_surface (private event queue for wl_shm + release events). If all shm buffers are busy the frame is dropped
  (otherwise tearing). Leak fixed: gralloc1 `unlock` returns a release fence fd that must be closed (hundreds of sync_file fds/s otherwise -> EMFILE -> RGXKickTA error 203).
- **Launcher:** `/usr/local/bin/gpuwin <cmd>` (sets EGL_PLATFORM=casper, SDL_VIDEODRIVER=wayland, WAYLAND_DISPLAY). Run it inside the sway session as siddh.
- **Tests:** `sdl_gles_test` (SDL2 GLES2 shader, 360x640) ~300-410 fps. **Chocolate Doom + Freedoom** (`SDL_RENDER_DRIVER=opengles2`), `-timedemo demo1`: 7117 gametics in
  1539 realtics = **161.9 fps** (real game speed is 35). Launcher entry "Freedoom (GPU)" in /usr/share/applications. New windows open on sway workspace 2 here.
- **Not yet:** X11/XWayland apps (native window is not a wl_egl_window -> renders unseen), resize handling beyond reallocating on width/height change, vsync/frame callbacks,
  desktop GL (only GLES), browsers.

## Update 2026-11-21 (late night): browser work - GTK4 UI on the GPU; WebKit page content is the CPU bottleneck

- **Epiphany (WebKitGTK 2.48, GTK4) UI now renders on the GPU**: `gpuwin env GDK_BACKEND=wayland GSK_RENDERER=ngl WEBKIT_DISABLE_DMABUF_RENDERER=1 epiphany <url>`. GTK creates a GLES 3.2 context on the PowerVR
  through hybris + the casper platform (aliased as `eglplatform_wayland.so`, since GTK asks for the Wayland platform explicitly).
  Needed: libhybris advertises `EGL_KHR_display_reference`/`EGL_EXT_device_base/query` (client + display strings) and the plugin stubs `eglQueryDisplayAttribEXT`,
  `eglQueryDeviceStringEXT`, `eglQueryDevicesEXT`, `eglQueryDeviceAttribEXT` (libepoxy aborts without them).
- **Plugin bugs fixed while doing it:** `Presenter::bufs` was not zero-initialised (crash on first resize with a dirty heap); window destroy callback (`wl_egl_window.destroy_window_callback`)
  registered so the plugin never touches a dead surface; present waits (<=64 ms) for a free wl_shm buffer instead of dropping a frame (GTK paces on wl_surface.frame callbacks - a dropped swap
  stalls it for ~1 s); PowerVR gralloc1 `lock` writes several pointers through outData - pass an 8-pointer array.
- **Sway does not send frame callbacks to windows on hidden workspaces**; new windows open on workspace 2 here, so animated apps appear frozen until you switch to it.
- **WebKit sandbox crashes on this kernel even with NO GPU stuff** (plain `epiphany` repeats "Web process crashed"); `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` fixes it (security tradeoff). CONFIG_USER_NS=y, so the cause is not obvious (bwrap).
- **Page content is CPU-bound:** `browser-fps.html` (rAF counter written to the title, read with `swaymsg -t get_tree`) runs ~1.2 fps even with 4 plain boxes; web process 91% inside libwebkitgtk
  (a tight pixel loop), UI process 54% in the kernel (shm memfd create/free, fd passing). WebKit's GPU path needs DMABuf buffers (gbm), which has no backend for this GPU, so
  `WEBKIT_DISABLE_DMABUF_RENDERER=1` falls back to CPU rendering of the page.
- **Kernel is built with heavy debugging:** PROVE_LOCKING, LOCKDEP, LOCK_STAT, DEBUG_SPINLOCK, DEBUG_MUTEXES, DEBUG_RWSEMS, DEBUG_ATOMIC_SLEEP, DEBUG_IRQFLAGS, PROVE_RCU, LATENCYTOP, DEBUG_PAGE_REF,
  KALLSYMS_ALL (lock_acquire/lock_release/rcu_is_watching show in perf). Turning them off should speed up everything that is syscall/lock heavy, but every out-of-tree module
  (pvrsrvkm, casper-*, wifi, touch, mt6357-jack...) must be rebuilt against the new config (lockdep changes struct sizes) and /lib/modules replaced; use a new LOCALVERSION so the old kernel+modules stay as a rollback.
- **Chromium 156 installed** (`--ozone-platform=wayland --use-gl=angle --use-angle=gles --no-sandbox`): ANGLE fails with "Failed to get system egl display" and never calls libhybris
  eglGetDisplay/eglGetPlatformDisplay (only eglGetProcAddress). Unresolved; libhybris `egl.c` now has CASPER_EGL_DEBUG logging and an unknown-platform fallback to the default ws.

## Update 2026-11-21 (very late): Firefox is the browser answer so far
- `firefox-esr` 153.5.1 (Debian) on Wayland through `gpuwin`: the same animated page that runs ~1.2 fps in Epiphany/WebKit runs **20-34 fps** in Firefox, also with the content sandbox ON.
  Profile with `gfx.webrender.all=true`; env `MOZ_ENABLE_WAYLAND=1 MOZ_WEBRENDER=1`. Command: `gpuwin firefox-esr --no-remote --profile <dir> <url>`.
- It is **software WebRender** for now: hardware WebRender fails ("Failed GL context creation for hardware WebRender"), GPU utilisation ~0%. libhybris now drops the robustness
  context attributes (PowerVR advertises EGL_EXT_create_context_robustness but rejects them); the context is then created and made current (surfaceless) fine, yet Firefox still reports
  "Failed to create EGLContext!: 0x3000" (EGL_SUCCESS) - cause not found yet (release build has no GL logging). Next: replicate Firefox's init in a test program, check required extensions.
- libhybris egl.c has CASPER_EGL_DEBUG logging for eglGetPlatformDisplay/eglGetProcAddress/eglInitialize/eglChooseConfig/eglCreateContext/eglCreatePbufferSurface/eglMakeCurrent.

## Update 2026-11-21 (night): Firefox HARDWARE WebRender works
- **Root cause of "Failed GL context creation for hardware WebRender":** Firefox resolves its GL entry points from `libGL.so` (libglvnd's desktop dispatcher, loaded for its GLX probe). With no GLX context its
  `glGetString(GL_VERSION)` returns NULL, so `GLContext::Init()` fails right after a perfectly good `eglMakeCurrent` and the context is destroyed (the "0x3000" in the message is EGL_SUCCESS).
  Found by tracing the Renderer thread with gdb (python script: break eglMakeCurrent, `nexti` loop logging bl/blr targets; the call right before the failure was `glGetString` in /lib/aarch64-linux-gnu/libGL.so).
- **Fix:** `ln -s libGLESv2.so.2 /usr/local/hybris/lib/libGL.so` (hybris dir comes first in gpuwin's LD_LIBRARY_PATH, so only gpuwin apps see it) + libhybris dropping the unsupported robustness context attributes.
- **Result** (`browser-fps.html`, 4 animated boxes, ~800x1500): software WR 34-37 fps (CPU ~224%) -> hardware WR 43-48 fps, GPU 21-35% busy, Firefox CPU ~130%. Full Firefox UI + video render correctly.
- **Launcher:** `/usr/local/bin/firefox-gpu` (repo copy `gpu/firefox-gpu`): MOZ_ENABLE_WAYLAND=1 MOZ_WEBRENDER=1, gpuwin, persistent profile `~/.config/firefox-gpu` (user.js forces gfx.webrender.all). The stock menu entry
  `/usr/share/applications/firefox-esr.desktop` now runs it (backup `.bak-gpu`). Content sandbox stays ON. Windows open on sway workspace 2.
- **Video (muted VP9 test, `browser-vid.html`-style page):** decode is still CPU-bound; GPU only 1-11% busy. 480p ~30 fps shown, 720p ~11-14 fps (60 fps source). YouTube at 30 fps content: 480p ok, 720p marginal.
- Debug aids left in libhybris egl.c (env CASPER_EGL_DEBUG, flag cached); the per-call CASPER_EGL_TRACE macro tracing was removed again (binding.h restored, libEGL/libGLESv2 rebuilt). `ffsim.c` mimics Firefox's EGL init.

## Update 2026-11-23: asynchronous presenter + decoder limits

- **Profile of 720p60 playback:** sway's single compositor thread sat at ~94% inside libpixman (software compositing plus the 90 degree output rotation); mpv's render thread only ~9%.
  With the output un-rotated sway fell to ~9% and the machine was mostly idle, yet mpv still dropped ~55% of frames: the limit was latency, not CPU.
- **Cause:** `queueBuffer()` of `eglplatform_casper` was fully synchronous: `sync_wait(fence)` (wait for the GPU), gralloc `lock`, 4.6 MB `memcpy`, commit, and only then did eglSwapBuffers return,
  so GPU render and CPU copy were serialised (~43 fps ceiling).
- **Fix:** a worker thread now does the fence wait, copy and commit (`present()`); `queueBuffer` just queues the buffer and returns. At most one frame waits behind the one being presented; a queued
  frame older than that is dropped after its fence signals (latest frame wins). `dequeueBuffer` drains the worker before it reallocates buffers on a resize; the destructor joins the worker.
  Build with `gpu/eglplatform-casper/build-on-phone.sh` (on the phone). Installed over BOTH `eglplatform_casper.so` and `eglplatform_wayland.so` in `/usr/local/hybris/lib/libhybris/`
  (gpuwin sets EGL_PLATFORM=wayland). Backups on the phone: `/root/eglplatform_casper.so.orig-2026-11-23` (the old casper build), `/root/eglplatform_casper.so.async-2026-11-23` (the new one).
  The old `eglplatform_wayland.so` (a separate inode) was replaced without a backup; it should have been a copy of the casper build.
- **Results (mpv gpu VO, silent, landscape sway):** 720p24 ~1% -> 0 drops; 1080p30 23% -> 0 drops; 720p60 28% -> ~10% drops. SDL test (same window): 84 -> 91-94 fps. Firefox starts and renders.
- **Decoder:** vdpipe decodes 720p60 at 75.6 fps and 1080p30 at 41 fps. It pads width AND height to multiples of 16 and never crops (hwplay crops with `--video-crop`).
  hwplay default is now <=1080p30. 720p60 still drops ~10%; the remaining limit is sway pixman rotation + copy.
- **Gotcha:** the 32-bit vendor decoder aborts when a pid is above 65535 (silent blank video); `kernel.pid_max=65535` in `/etc/sysctl.d/99-pid-max.conf` on the phone.

## Update 2026-11-23 (later): the stutter was sway's landscape rotation, not the decoder or the EGL layer

- **How it was found:** `CASPER_STATS=1` makes the casper platform log queueBuffer/commit/dequeue timestamps to `/tmp/casper-stats.<pid>.txt` (written when the worker thread stops, i.e. clean mpv exit; `CASPER_SYNC=1`
  forces the old synchronous present). mpv's own counters (frame-drop-count, estimated-vf-fps) all looked perfect while the swap log showed 8.1 swaps/s in a fixed rhythm (41 41 375 41 41 208 ms).
  mpv option changes (sync mode, vsync, threads, cache) did not change it at all. With the output un-rotated (`swaymsg output * transform normal`, picture rotated by mpv `--video-rotate=90`) the same clip swaps at a
  steady 24.0/s with 41-43 ms spacing. So with `output * transform 90` (landscape UI) sway's software (pixman) rotation + the kernel's XRGB->ARGB copy makes sway the pacing source (frame callbacks) and
  mpv silently skips ~2/3 of the frames without counting drops. All earlier "drops" tables above under-reported this.
- **Fix:** `hwplay` switches the sway output to `transform normal` while it runs and rotates with `--video-rotate=<90|270>` (+ `--osc=no`), restoring the transform in its exit trap (`HWPLAY_NOROTATE=1` disables).
  Recovery if hwplay is SIGKILLed: `swaymsg 'output * transform 90'`. The seek gestures use the window's y axis in this mode (`hwplay-rot`, `hwplay-rotdir` script-opts).
- **Result (swaps per second actually reaching the compositor):** 720p24 24.0 (even), 1080p30 24.5, 720p60 44.9. yt-dlp format order now prefers 1080p only for <=25 fps, else 720p30.
- **Unfixed:** sway is still the limit at 45 fps; a GPU compositor path (or kernel plane rotation) would lift it. `eglplatform_wayland.so`/`eglplatform_casper.so` installed on the phone are the stats-capable async build.

## Update 2026-11-23 (night): the real limit was mpv's default GPU shaders, not the CPU

- **Gentle CPU boost test (casper-mt6765-dvfs, nd kernel, loaded by hand):** L cluster (cpu0-3) 1.5 -> 1.55 -> 1.6 GHz at the unchanged 800 mV rail (stock table says 1.617 GHz needs 768 mV), 4-core hash/arith stress: 0 errors,
  throughput scaled with clock (+3.3% per step). Higher clocks need >800 mV (1.814 GHz = 837 mV) and the old guarded attempt (L 1.879 GHz @875 mV + LL 1.226 GHz) left `/var/lib/casper-dvfs.trying` on Nov 21
  (the phone hung/reset), so it was not repeated. There is no SoC thermal sensor (only the charger's). Module unloaded and L set back to 1.5 GHz after the test; nothing persists across a reboot.
- **Effect of the boost on the hard clips:** none while the GPU was the limit (720p60 44.9 -> 45.2 fps, 1080p30 24.5 -> 24.6).
- **Actual bottleneck:** mpv's default quality path (lanczos chroma scaling, rgba16f intermediate FBO, dither) costs the PowerVR GE8320 ~20-40 ms per frame. With `--profile=fast --fbo-format=rgba8`:
  720p60 presents at 60.0 swaps/s (every gap 16.7 ms), 1080p30 at 30.1 (33.3 ms), 720p24 24.0. hwplay now uses those options; with the CPU at stock 1.5 GHz 720p60 gets 57.0 (CPU matters again at the margin: 1.6 GHz gave 60.0).
  hwplay's yt-dlp order: <=1080p30 first, else 720p60. Measure with `CASPER_STATS=1` swap logs, not mpv counters.

## Update 2026-11-23 (end): fill mode, rotation timing, 1.6 GHz at boot

- **hwplay screen fill:** the 16:9 (or 4:3-ish) picture fits the 20:9 panel's short side, so it left black bars on both ends of the long side (65-80% of the screen used). hwplay now plays with `--panscan=1`
  (zoom to fill, crops the overflow); `z` toggles fit/fill, `HWPLAY_PANSCAN=0` or `--panscan=0` in `~/.config/hwplay/mpv-opts` for the whole picture.
- **Rotation timing:** hwplay used to switch the sway output to portrait as soon as the URL was resolved, but mpv takes ~10 s to open its window here, so Firefox was visible sideways/half drawn meanwhile. It now opens the
  (landscape, fullscreen) mpv window first and rotates after. An independent watcher restores landscape within ~1 s of hwplay exiting for any reason (SIGKILL included) unless a newer hwplay owns `$XDG_RUNTIME_DIR/hwplay.rot`.
- **1.6 GHz at boot:** `casper-dvfs.service` (Type=simple, no 60 s boot delay) runs `casper-dvfs-up`: loads the nd module and sets only cpu0-3 to 1.6 GHz at the bootloader's 800 mV rail; guard file as before
  (the stale Nov 21 guard was removed). Verified by running the service live, NOT yet by a reboot. The old aggressive version is saved on the phone as `/root/casper-dvfs-up.orig-2026-11-23`.

## Update 2026-11-23 (final): landscape-only, the real cause was the pixel format

- **Root cause of the whole "sway cannot rotate fast enough" saga:** the casper platform presented `WL_SHM_FORMAT_XBGR8888`. pixman (sway's software renderer) only has its fast blit/rotate paths for the
  standard formats, so rotating a fullscreen XBGR8888 buffer for the landscape output (`transform 90`) cost ~100 ms (8 fps). The platform now swaps R and B while it copies and presents `XRGB8888`
  (`CASPER_XBGR=1` restores the old format). With that, in landscape: 720p60 -> 59.9 swaps/s, 1080p30 -> 30.1, 720p24 -> 24.1; the worker thread needs ~8 ms per frame; sway CPU is no longer pinned.
- **Tried and dropped:** pre-rotating the buffer in the platform and declaring it with `wl_surface.set_buffer_transform` (correct picture with `PRETRANSFORM=90`, pixels counter-clockwise, but no faster: 24 swaps/s and
  sway still in the same pixman loop), and the portrait-output trick (hwplay switching sway to `transform normal` + `--video-rotate`). Both removed. hwplay no longer touches the output transform at all; the screen
  stays in landscape. The previous version with the flip is saved on the phone as `/root/hwplay.with-rotation-2026-11-23` and in the repo history of this session (`hwplay.with-rotation` in the scratchpad).
- **Installed library:** `/usr/local/hybris/lib/libhybris/eglplatform_{casper,wayland}.so` = XRGB build (backup `/root/eglplatform_casper.so.xrgb-2026-11-23`; older: `.orig-2026-11-23`, `.async-2026-11-23`, `.rot-2026-11-23`).
  Verified: mpv/hwplay (colors, orientation, fill), SDL test, Firefox (RED/GREEN/BLUE page correct).

## 2026-11-23 (late): status, touch toggle, reboot test

- **Status: experimental, not finished.** Works day to day. Still software or CPU-bound: sway composites with pixman, every frame is CPU-copied into wl_shm (about 8 ms), Firefox hands video to mpv instead of decoding it, decode tops out near 1080p30 / 720p60.
- **Touch toggle:** double-tap the top-right corner of a video (top quarter, right third) switches fill and fit. `z` still works. Fill stays the default.
- **1.6 GHz boot service verified across a real reboot** (2026-11-23 ~20:57): `casper-dvfs` active, L cluster at 1600000 kHz about 19 s after boot (log: `cluster L cpu0-3 -> 1600000 kHz`), LL cluster 920 MHz, rail 800 mV, no stale `/var/lib/casper-dvfs.trying`, `casper-gpu` active, pocketshell GPU build running. No hang with USB plugged in. The USB network link (172.16.42.1) did not come back after the reboot; the phone was reachable on Wi-Fi (<lan-ip>). Cause not investigated.
