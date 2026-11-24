// GLES2 throughput on the GE8320 via libhybris: draw (glFinish) vs draw + glReadPixels, per resolution.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static GLuint sh(GLenum t,const char*s){GLuint h=glCreateShader(t);glShaderSource(h,1,&s,0);glCompileShader(h);return h;}
int main(int argc,char**argv){
  int W=argc>1?atoi(argv[1]):640,H=argc>2?atoi(argv[2]):360;
  EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);eglInitialize(d,0,0);
  const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig cfg;EGLint n;eglChooseConfig(d,ca,&cfg,1,&n);
  const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
  EGLSurface s=eglCreatePbufferSurface(d,cfg,pa);
  const EGLint xa[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext c=eglCreateContext(d,cfg,EGL_NO_CONTEXT,xa);
  if(!eglMakeCurrent(d,s,s,c)){printf("makecurrent fail 0x%x\n",eglGetError());return 1;}
  printf("%s %dx%d\n",glGetString(GL_RENDERER),W,H);
  GLuint p=glCreateProgram();
  glAttachShader(p,sh(GL_VERTEX_SHADER,"attribute vec2 a;varying vec2 v;void main(){v=a;gl_Position=vec4(a,0.,1.);}"));
  glAttachShader(p,sh(GL_FRAGMENT_SHADER,"precision mediump float;varying vec2 v;uniform float t;void main(){vec2 q=v*3.;float x=sin(q.x+t)+cos(q.y*1.3-t);gl_FragColor=vec4(.5+.5*sin(x*2.),.5+.5*cos(x),.5+.5*sin(x+q.x),1.);}"));
  glLinkProgram(p);glUseProgram(p);
  float q[]={-1,-1,1,-1,-1,1,1,1};glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,0,0,q);
  GLint tu=glGetUniformLocation(p,"t");
  unsigned char*buf=malloc(W*H*4);
  GLuint pbo[2];glGenBuffers(2,pbo);for(int i=0;i<2;i++){glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo[i]);glBufferData(GL_PIXEL_PACK_BUFFER,W*H*4,0,GL_STREAM_READ);}glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
  for(int mode=0;mode<4;mode++){
    int f=0;double t0=now();
    while(now()-t0<4){glUniform1f(tu,f*0.05f);for(int k=0;k<4;k++)glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      if(mode==0)glFinish();
      else if(mode==1)glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,buf);
      else if(mode==2){glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo[0]);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,0);
        void*m=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,W*H*4,GL_MAP_READ_BIT);if(m)memcpy(buf,m,W*H*4);glUnmapBuffer(GL_PIXEL_PACK_BUFFER);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);}
      else{int a=f&1,b=a^1;glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo[a]);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER,pbo[b]);void*m=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,W*H*4,GL_MAP_READ_BIT);if(m)memcpy(buf,m,W*H*4);glUnmapBuffer(GL_PIXEL_PACK_BUFFER);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);}
      f++;}
    static const char*N[]={"draw only","glReadPixels","PBO sync","PBO double-buffered"};printf("%s: %.1f fps\n",N[mode],f/(now()-t0));
  }
  return 0;}
