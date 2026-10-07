#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(void)
{
    int fd = open("/dev/dri/card0", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("open"); return 1; }
    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) { perror("resources"); return 2; }
    for (int i = 0; i < resources->count_crtcs; ++i) {
        drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[i]);
        if (!crtc) continue;
        printf("CRTC=%u WIDTH=%u HEIGHT=%u FB=%u X=%u Y=%u\n", crtc->crtc_id, crtc->width, crtc->height, crtc->buffer_id, crtc->x, crtc->y);
        drmModeFB2 *fb = drmModeGetFB2(fd, crtc->buffer_id);
        if (fb) {
            printf("FB=%u WIDTH=%u HEIGHT=%u PITCH=%u FORMAT=%08x MODIFIER=%" PRIu64 "\n", fb->fb_id, fb->width, fb->height, fb->pitches[0], fb->pixel_format, fb->modifier);
            drmModeFreeFB2(fb);
        }
        drmModeFreeCrtc(crtc);
    }
    drmModePlaneRes *planes = drmModeGetPlaneResources(fd);
    if (planes) for (unsigned int i = 0; i < planes->count_planes; ++i) {
        drmModeObjectProperties *props = drmModeObjectGetProperties(fd, planes->planes[i], DRM_MODE_OBJECT_PLANE);
        if (!props) continue;
        for (unsigned int j = 0; j < props->count_props; ++j) {
            drmModePropertyRes *property = drmModeGetProperty(fd, props->props[j]);
            if (!property) continue;
            printf("PLANE=%u PROPERTY=%s VALUE=%" PRIu64 "\n", planes->planes[i], property->name, props->prop_values[j]);
            if ((property->flags & DRM_MODE_PROP_BLOB) && props->prop_values[j]) {
                drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(fd, props->prop_values[j]);
                if (blob) {
                    const uint16_t *data = blob->data;
                    printf("BLOB_BYTES=%u VALUES=", blob->length);
                    for (unsigned int k = 0; k < blob->length / 2 && k < 32; ++k) printf("%u,", data[k]);
                    puts("");
                    drmModeFreePropertyBlob(blob);
                }
            }
            drmModeFreeProperty(property);
        }
        drmModeFreeObjectProperties(props);
    }
    drmModeFreePlaneResources(planes);
    drmModeFreeResources(resources);
    close(fd);
    return 0;
}
