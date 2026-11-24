// Headless EGL+GLES2 smoke test, run on bionic against the MTK/PowerVR vendor driver.
#define _GNU_SOURCE
#include <dlfcn.h>
extern void *android_dlopen(const char*,int); extern void *android_dlsym(void*,const char*); extern const char *android_dlerror(void);
#define dlopen android_dlopen
#define dlsym android_dlsym
#define dlerror android_dlerror
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;
typedef int32_t EGLint; typedef unsigned EGLBoolean;
#define EGL_DEFAULT_DISPLAY 0
#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 1
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 4
#define EGL_NONE 0x3038
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define L(h,n) n##_t n = (n##_t)dlsym(h,#n); if(!n){printf("missing %s: %s\n",#n,dlerror());return 2;}
typedef EGLDisplay (*eglGetDisplay_t)(void*); typedef EGLBoolean (*eglInitialize_t)(EGLDisplay,EGLint*,EGLint*);
typedef EGLBoolean (*eglChooseConfig_t)(EGLDisplay,const EGLint*,EGLConfig*,EGLint,EGLint*);
typedef EGLSurface (*eglCreatePbufferSurface_t)(EGLDisplay,EGLConfig,const EGLint*);
typedef EGLContext (*eglCreateContext_t)(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
typedef EGLBoolean (*eglMakeCurrent_t)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
typedef const char* (*eglQueryString_t)(EGLDisplay,EGLint); typedef EGLint (*eglGetError_t)(void);
typedef EGLBoolean (*eglBindAPI_t)(unsigned); typedef EGLBoolean (*eglGetConfigs_t)(EGLDisplay,EGLConfig*,EGLint,EGLint*);
typedef void (*glClearColor_t)(float,float,float,float); typedef void (*glClear_t)(unsigned); typedef void (*glFinish_t)(void);
typedef void (*glReadPixels_t)(int,int,int,int,unsigned,unsigned,void*); typedef const unsigned char* (*glGetString_t)(unsigned);
int main(void){
  setvbuf(stdout,0,_IONBF,0);
  void *e=dlopen("libEGL_mtk.so",RTLD_NOW|RTLD_GLOBAL); if(!e){printf("dlopen EGL: %s\n",dlerror());return 1;}
  printf("EGL driver loaded\n");
  void *g=dlopen("libGLESv2_mtk.so",RTLD_NOW|RTLD_GLOBAL); if(!g){printf("dlopen GLES: %s\n",dlerror());return 1;}
  printf("GLES driver loaded\n");
  L(e,eglGetDisplay) L(e,eglInitialize) L(e,eglChooseConfig) L(e,eglCreatePbufferSurface) L(e,eglCreateContext) L(e,eglMakeCurrent) L(e,eglQueryString) L(e,eglGetError) L(e,eglBindAPI) L(e,eglGetConfigs)
  L(g,glClearColor) L(g,glClear) L(g,glFinish) L(g,glReadPixels) L(g,glGetString)
  EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY); printf("display=%p err=0x%x\n",d,eglGetError());
  EGLint maj=0,min=0; if(!eglInitialize(d,&maj,&min)){printf("eglInitialize failed 0x%x\n",eglGetError());return 3;}
  printf("EGL %d.%d vendor=%s\n",maj,min,eglQueryString(d,EGL_VENDOR));
  { EGLint nc=-1; EGLBoolean r=eglGetConfigs(d,0,0,&nc); printf("eglGetConfigs -> %u, count=%d err=0x%x\n",r,nc,eglGetError()); }
  EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg; EGLint n=0; if(!eglChooseConfig(d,ca,&cfg,1,&n)||n<1){printf("no config err 0x%x n=%d\n",eglGetError(),n);return 4;}
  EGLint pa[]={EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE}; EGLSurface s=eglCreatePbufferSurface(d,cfg,pa); printf("pbuffer=%p err=0x%x\n",s,eglGetError());
  eglBindAPI(0x30A0);
  EGLint xa[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE}; EGLContext c=eglCreateContext(d,cfg,0,xa); printf("context=%p err=0x%x\n",c,eglGetError());
  if(!eglMakeCurrent(d,s,s,c)){printf("makecurrent failed 0x%x\n",eglGetError());return 5;}
  printf("GL renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
  glClearColor(0.25f,0.5f,0.75f,1.0f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
  unsigned char px[4]={0}; glReadPixels(10,10,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
  printf("pixel RGBA = %u %u %u %u (expect ~64 128 191 255)\n",px[0],px[1],px[2],px[3]);
  return 0;
}
