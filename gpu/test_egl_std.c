// Standard glibc EGL + GLES2 program: linked against libEGL/libGLESv2 (libhybris wrappers), no hybris API used.
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
int main(void) {
  setvbuf(stdout, 0, _IONBF, 0);
  EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint maj, min;
  if (!eglInitialize(d, &maj, &min)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
  printf("EGL %d.%d vendor=%s\n", maj, min, eglQueryString(d, EGL_VENDOR));
  const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
  EGLConfig cfg; EGLint n = 0;
  if (!eglChooseConfig(d, ca, &cfg, 1, &n) || !n) { printf("no config 0x%x\n", eglGetError()); return 2; }
  const EGLint pa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
  EGLSurface s = eglCreatePbufferSurface(d, cfg, pa);
  const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
  if (!eglMakeCurrent(d, s, s, c)) { printf("makecurrent failed 0x%x\n", eglGetError()); return 3; }
  printf("GL_RENDERER=%s\nGL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
  glClearColor(0.25f, 0.5f, 0.75f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
  unsigned char px[4]; glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  printf("pixel = %d %d %d %d (expect ~64 128 191 255)\n", px[0], px[1], px[2], px[3]);
  return 0;
}
