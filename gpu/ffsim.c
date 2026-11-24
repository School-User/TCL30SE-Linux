// Mimic Firefox's EGL ES3 surfaceless init against hybris and check GL symbol resolution.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
typedef const GLubyte *(*gs_t)(GLenum);
int main(void)
{
	setvbuf(stdout, 0, _IONBF, 0);
	void *lib = dlopen("libEGL.so", RTLD_LAZY | RTLD_LOCAL);
	void *(*gpa)(const char *) = dlsym(lib, "eglGetProcAddress");
	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint ma, mi; printf("init=%d\n", eglInitialize(d, &ma, &mi));
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig cfg; EGLint n; printf("choose=%d n=%d\n", eglChooseConfig(d, ca, &cfg, 1, &n), n);
	EGLint xa[] = { 0x3098, 3, 0x31bd, 0x31bf, 0x30fc, 4, EGL_NONE };
	EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, xa);
	printf("ctx=%p err=0x%x\n", c, eglGetError());
	printf("makecurrent=%d err=0x%x\n", eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c), eglGetError());
	const char *names[] = { "glGetString", "glGetIntegerv", "glGetError", "glBlitFramebuffer", "glBindVertexArray", "glTexStorage2D", "glGetStringi", "glGetGraphicsResetStatusKHR", "glDebugMessageControlKHR", "glFenceSync" };
	for (int i = 0; i < 10; i++) printf("gpa(%s)=%p dlsym(libEGL)=%p\n", names[i], gpa(names[i]), dlsym(lib, names[i]));
	gs_t gs = (gs_t)gpa("glGetString");
	if (gs) printf("GL_VERSION=%s\nGL_RENDERER=%s\nGL_VENDOR=%s\n", gs(GL_VERSION), gs(GL_RENDERER), gs(GL_VENDOR));
	return 0;
}
