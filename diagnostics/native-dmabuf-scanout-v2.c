#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

/* GPU render -> exported DMA-BUF -> Tegra KMS scanout.
 * No gbm_bo_map, mmap, glReadPixels or CPU pixel-copy operation is used.
 * Run only on the temporary native console boot, without a compositor.
 */
struct buffer { struct gbm_bo *bo; EGLImageKHR image; GLuint texture, fbo; uint32_t handle, fb; };
static bool pending;
static void flipped(int fd, unsigned frame, unsigned sec, unsigned usec, void *data) {
    (void)fd;(void)frame;(void)sec;(void)usec;(void)data;
    pending = false;
}
static double now(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9; }
static bool driver_is(int fd, const char *name) {
    drmVersion *v=drmGetVersion(fd);
    bool match=v && !strcmp(v->name,name);
    if(v)drmFreeVersion(v);
    return match;
}
int main(int argc, char **argv) {
    int result=1, kms=-1, gpu=-1, frames=0;
    uint32_t connector_id=0, crtc_id=0;
    drmModeRes *resources=NULL;
    drmModeConnector *connector=NULL;
    drmModeCrtc *saved=NULL;
    struct gbm_device *gbm=NULL;
    struct buffer buffers[2]={0};
    EGLDisplay display=EGL_NO_DISPLAY;
    EGLContext context=EGL_NO_CONTEXT;
    bool modeset=false;
    PFNEGLCREATEIMAGEKHRPROC create_image=NULL;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image=NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target=NULL;
    if(argc!=3){fprintf(stderr,"usage: %s /dev/dri/cardTEGRA /dev/dri/renderDNOUVEAU\n",argv[0]);return 2;}
    FILE *cmd=fopen("/proc/cmdline","r");char command_line[4096]={0};
    if(cmd){fgets(command_line,sizeof(command_line),cmd);fclose(cmd);}
    if(!strstr(command_line,"mocha_native_debug4=1")){fprintf(stderr,"temporary native boot required\n");return 2;}
    memset(command_line,0,sizeof(command_line));
    kms=open(argv[1],O_RDWR|O_CLOEXEC);gpu=open(argv[2],O_RDWR|O_CLOEXEC);
    if(kms<0||gpu<0||!driver_is(kms,"tegra")||!driver_is(gpu,"nouveau")) {fprintf(stderr,"unexpected DRM devices\n");goto done;}
    resources=drmModeGetResources(kms);
    if(!resources)goto done;
    for(int i=0;i<resources->count_connectors;i++) {
        drmModeConnector *c=drmModeGetConnector(kms,resources->connectors[i]);
        if(c && c->connector_type==DRM_MODE_CONNECTOR_DSI && c->connection==DRM_MODE_CONNECTED && c->count_modes) {connector=c;break;}
        if(c)drmModeFreeConnector(c);
    }
    if(!connector){fprintf(stderr,"no connected native DSI output\n");goto done;}
    connector_id=connector->connector_id;
    drmModeEncoder *encoder=drmModeGetEncoder(kms,connector->encoder_id);
    if(encoder){crtc_id=encoder->crtc_id;drmModeFreeEncoder(encoder);}
    if(!crtc_id){fprintf(stderr,"native console has no active CRTC\n");goto done;}
    saved=drmModeGetCrtc(kms,crtc_id);
    if(!saved||!saved->mode_valid){fprintf(stderr,"no console state to restore\n");goto done;}
    drmModeModeInfo mode=saved->mode;
    if(mode.hdisplay!=1536||mode.vdisplay!=2048){fprintf(stderr,"unexpected panel dimensions\n");goto done;}
    gbm=gbm_create_device(gpu);
    if(!gbm)goto done;
    display=eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR,gbm,NULL);
    if(display==EGL_NO_DISPLAY||!eglInitialize(display,NULL,NULL)||!eglBindAPI(EGL_OPENGL_ES_API)){fprintf(stderr,"EGL display initialization failed: %x\n",eglGetError());goto done;}
    EGLConfig config;EGLint count;
    const EGLint config_attrs[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_SURFACE_TYPE,EGL_WINDOW_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    if(!eglChooseConfig(display,config_attrs,&config,1,&count)||count!=1){fprintf(stderr,"GBM EGL window config unavailable: count=%d error=%x\n",count,eglGetError());goto done;}
    const EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
    if(context==EGL_NO_CONTEXT||!eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context)){fprintf(stderr,"EGL surfaceless context failed: %x\n",eglGetError());goto done;}
    create_image=(PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    destroy_image=(PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    image_target=(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if(!create_image||!destroy_image||!image_target)goto done;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("GPU_RENDERER=%s\n",renderer?renderer:"unknown");
    if(!renderer||strstr(renderer,"llvmpipe")||strstr(renderer,"softpipe")){fprintf(stderr,"hardware renderer required\n");goto done;}
    for(int i=0;i<2;i++) {
        struct buffer *b=&buffers[i];
        b->bo=gbm_bo_create(gbm,mode.hdisplay,mode.vdisplay,GBM_FORMAT_XRGB8888,GBM_BO_USE_RENDERING|GBM_BO_USE_LINEAR);
        if(!b->bo){fprintf(stderr,"GPU linear allocation failed\n");goto done;}
        if(gbm_bo_get_plane_count(b->bo)!=1 || gbm_bo_get_modifier(b->bo)!=DRM_FORMAT_MOD_LINEAR){fprintf(stderr,"linear single-plane DMA-BUF required\n");goto done;}
        const EGLint attrs[]={EGL_IMAGE_PRESERVED_KHR,EGL_TRUE,EGL_NONE};
        b->image=create_image(display,EGL_NO_CONTEXT,EGL_NATIVE_PIXMAP_KHR,(EGLClientBuffer)b->bo,attrs);
        if(b->image==EGL_NO_IMAGE_KHR){fprintf(stderr,"GPU EGL image failed: %x\n",eglGetError());goto done;}
        glGenTextures(1,&b->texture);glBindTexture(GL_TEXTURE_2D,b->texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        image_target(GL_TEXTURE_2D,b->image);
        glGenFramebuffers(1,&b->fbo);glBindFramebuffer(GL_FRAMEBUFFER,b->fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,b->texture,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"GPU framebuffer incomplete\n");goto done;}
        int dmafd=gbm_bo_get_fd(b->bo);
        if(dmafd<0)goto done;
        int imported=drmPrimeFDToHandle(kms,dmafd,&b->handle);close(dmafd);
        if(imported){fprintf(stderr,"Tegra DMA-BUF import failed: %s\n",strerror(errno));goto done;}
        uint32_t handles[4]={b->handle},pitches[4]={gbm_bo_get_stride(b->bo)},offsets[4]={0};
        if(drmModeAddFB2(kms,mode.hdisplay,mode.vdisplay,DRM_FORMAT_XRGB8888,handles,pitches,offsets,&b->fb,0)){fprintf(stderr,"Tegra DMA-BUF framebuffer failed: %s\n",strerror(errno));goto done;}
        printf("DMA_BUF_%d pitch=%u fb=%u linear=1 imported=1\n",i,pitches[0],b->fb);
        glClearColor(.12f,.12f,.12f,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
    }
    if(drmModeSetCrtc(kms,crtc_id,buffers[0].fb,0,0,&connector_id,1,&mode)){fprintf(stderr,"native GPU scanout modeset failed: %s\n",strerror(errno));goto done;}
    modeset=true;
    printf("NATIVE_GPU_SCANOUT_STARTED mode=%s clock_khz=%u\n",mode.name,mode.clock);fflush(stdout);
    drmEventContext events={.version=2,.page_flip_handler=flipped};
    double start=now();
    while(now()-start<5) {
        struct buffer *b=&buffers[(frames+1)%2];
        glBindFramebuffer(GL_FRAMEBUFFER,b->fbo);glViewport(0,0,mode.hdisplay,mode.vdisplay);
        glEnable(GL_SCISSOR_TEST);
        for(int quadrant=0;quadrant<4;quadrant++) {
            glScissor((quadrant%2)*mode.hdisplay/2,(quadrant/2)*mode.vdisplay/2,mode.hdisplay/2,mode.vdisplay/2);
            float pulse=.25f+.4f*(frames%60)/60.f;
            glClearColor(quadrant==0?pulse:.08f,quadrant==1?pulse:.08f,quadrant==2?pulse:.08f,1);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        glDisable(GL_SCISSOR_TEST);glFinish();
        if(glGetError()!=GL_NO_ERROR){fprintf(stderr,"GPU rendering failed\n");goto done;}
        if(drmModePageFlip(kms,crtc_id,b->fb,DRM_MODE_PAGE_FLIP_EVENT,NULL)){fprintf(stderr,"page flip failed: %s\n",strerror(errno));goto done;}
        pending=true;
        struct pollfd pfd={.fd=kms,.events=POLLIN};
        double deadline=now()+1;
        while(pending) {
            if(now()>deadline||poll(&pfd,1,100)<0){fprintf(stderr,"native vblank wait failed\n");goto done;}
            if(pfd.revents&POLLIN){if(drmHandleEvent(kms,&events)<0)goto done;}
        }
        frames++;
    }
    printf("NATIVE_GPU_SCANOUT_FRAMES=%d SECONDS=%.3f FPS=%.2f CPU_PIXEL_COPIES=0\n",frames,now()-start,frames/(now()-start));
    result=0;
done:
    if(modeset && drmModeSetCrtc(kms,saved->crtc_id,saved->buffer_id,saved->x,saved->y,&connector_id,1,&saved->mode)){fprintf(stderr,"console restore failed: %s\n",strerror(errno));result=1;}
    for(int i=0;i<2;i++) {
        struct buffer *b=&buffers[i];
        if(b->fb)drmModeRmFB(kms,b->fb);
        if(b->handle)drmCloseBufferHandle(kms,b->handle);
        if(b->fbo)glDeleteFramebuffers(1,&b->fbo);
        if(b->texture)glDeleteTextures(1,&b->texture);
        if(b->image && destroy_image)destroy_image(display,b->image);
        if(b->bo)gbm_bo_destroy(b->bo);
    }
    if(display!=EGL_NO_DISPLAY){eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);if(context!=EGL_NO_CONTEXT)eglDestroyContext(display,context);eglTerminate(display);}
    if(gbm)gbm_device_destroy(gbm);
    if(saved)drmModeFreeCrtc(saved);
    if(connector)drmModeFreeConnector(connector);
    if(resources)drmModeFreeResources(resources);
    if(gpu>=0)close(gpu);
    if(kms>=0)close(kms);
    return result;
}
