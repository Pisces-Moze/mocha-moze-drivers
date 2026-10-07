#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"
static struct wl_compositor *compositor;
static struct xdg_wm_base *wm;
static struct wl_surface *surface;
static int configured, ready, width=1280, height=720, closed;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
static void ping(void*d,struct xdg_wm_base*w,uint32_t s){xdg_wm_base_pong(w,s);}
static const struct xdg_wm_base_listener wml={ping};
static void global(void*d,struct wl_registry*r,uint32_t id,const char*n,uint32_t v){
 if(!strcmp(n,"wl_compositor"))compositor=wl_registry_bind(r,id,&wl_compositor_interface,3);
 if(!strcmp(n,"xdg_wm_base")){wm=wl_registry_bind(r,id,&xdg_wm_base_interface,1);xdg_wm_base_add_listener(wm,&wml,NULL);}
}
static void removed(void*d,struct wl_registry*r,uint32_t id){}
static const struct wl_registry_listener rl={global,removed};
static void conf(void*d,struct xdg_surface*s,uint32_t serial){xdg_surface_ack_configure(s,serial);configured=1;}
static const struct xdg_surface_listener sl={conf};
static void topconf(void*d,struct xdg_toplevel*t,int32_t w,int32_t h,struct wl_array*a){if(w>0&&h>0){width=w;height=h;}}
static void closewin(void*d,struct xdg_toplevel*t){closed=1;}
static const struct xdg_toplevel_listener tl={topconf,closewin};
static void frame(void*d,struct wl_callback*c,uint32_t t){wl_callback_destroy(c);ready=1;}
static const struct wl_callback_listener fl={frame};
int main(int argc,char**argv){
 struct wl_display*d=wl_display_connect(NULL);if(!d)return 1;
 struct wl_registry*r=wl_display_get_registry(d);wl_registry_add_listener(r,&rl,NULL);wl_display_roundtrip(d);
 if(!wm||!compositor)return 2;
 surface=wl_compositor_create_surface(compositor);
 struct xdg_surface*xs=xdg_wm_base_get_xdg_surface(wm,surface);xdg_surface_add_listener(xs,&sl,NULL);
 struct xdg_toplevel*top=xdg_surface_get_toplevel(xs);xdg_toplevel_add_listener(top,&tl,NULL);
 xdg_toplevel_set_title(top,"Mocha graphics timing test");xdg_toplevel_set_app_id(top,"mocha-frame-bench");xdg_toplevel_set_fullscreen(top,NULL);
 wl_surface_commit(surface);while(!configured)if(wl_display_dispatch(d)<0)return 3;
 struct wl_egl_window*win=wl_egl_window_create(surface,width,height);
 EGLDisplay ed=eglGetDisplay((EGLNativeDisplayType)d);EGLint major,minor;
 if(!eglInitialize(ed,&major,&minor))return 4;
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,0,EGL_NONE};
 EGLConfig ec;EGLint num;eglChooseConfig(ed,attrs,&ec,1,&num);eglBindAPI(EGL_OPENGL_ES_API);
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};EGLContext ctx=eglCreateContext(ed,ec,EGL_NO_CONTEXT,ca);
 EGLSurface es=eglCreateWindowSurface(ed,ec,(EGLNativeWindowType)win,NULL);if(!eglMakeCurrent(ed,es,es,ctx))return 5;
 eglSwapInterval(ed,0);printf("RENDERER=%s WINDOW=%dx%d\n",glGetString(GL_RENDERER),width,height);fflush(stdout);
 double start=now(),last=start, maxgap=0,totalSwap=0;int count=0,slow=0;
 ready=1;
 while(!closed&&now()-start<15){
  while(!ready&&!closed)if(wl_display_dispatch(d)<0)return 6;
  ready=0;struct wl_callback*cb=wl_surface_frame(surface);wl_callback_add_listener(cb,&fl,NULL);
  glViewport(0,0,width,height);glClearColor((count%60)/60.0,.15,.35,1);glClear(GL_COLOR_BUFFER_BIT);
  double before=now();if(!eglSwapBuffers(ed,es))return 7;totalSwap+=now()-before;
  double t=now(),gap=t-last;if(count>5){if(gap>maxgap)maxgap=gap;if(gap>.025)slow++;}last=t;count++;
 }
 printf("FRAMES=%d SECONDS=%.3f FPS=%.2f MEAN_SWAP_MS=%.2f MAX_FRAME_GAP_MS=%.2f GAPS_OVER_25MS=%d\n",count,now()-start,count/(now()-start),1000*totalSwap/count,1000*maxgap,slow);
 eglMakeCurrent(ed,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(ed,es);eglDestroyContext(ed,ctx);eglTerminate(ed);wl_egl_window_destroy(win);xdg_toplevel_destroy(top);xdg_surface_destroy(xs);wl_surface_destroy(surface);wl_display_disconnect(d);return 0;
}
