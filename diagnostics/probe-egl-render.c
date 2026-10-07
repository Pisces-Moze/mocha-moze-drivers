#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>

static GLuint shader(GLenum type, const char *source) {
    GLuint object = glCreateShader(type);
    glShaderSource(object, 1, &source, NULL);
    glCompileShader(object);
    GLint ok = 0;
    glGetShaderiv(object, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(object, sizeof(log), NULL, log); printf("SHADER_FAIL=%s\n", log); }
    return ok ? object : 0;
}
int main(void) {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay display = platform ? platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    if (!eglInitialize(display, &major, &minor)) { printf("EGL_INIT_FAIL=%x\n", eglGetError()); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig config;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count) return 2;
    EGLint size[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLSurface surface = eglCreatePbufferSurface(display, config, size);
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    if (!eglMakeCurrent(display, surface, surface, context)) { printf("EGL_CONTEXT_FAIL=%x\n", eglGetError()); return 3; }
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    printf("RENDERER=%s\nGL_VERSION=%s\n", renderer, glGetString(GL_VERSION));
    GLuint vertex = shader(GL_VERTEX_SHADER, "attribute vec2 position; void main(){ gl_Position=vec4(position,0.0,1.0); }");
    GLuint fragment = shader(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor=vec4(1.0,0.0,0.0,1.0); }");
    if (!vertex || !fragment) return 4;
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position"); glLinkProgram(program);
    GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) return 5;
    const GLfloat vertices[] = {-1,-1,1,-1,-1,1,1,1};
    glViewport(0,0,16,16); glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices); glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    unsigned char pixel[4] = {0}; glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    GLenum error = glGetError();
    int rendered = pixel[0] > 240 && pixel[1] < 10 && pixel[2] < 10 && !error;
    int hardware = renderer && !strstr(renderer,"llvmpipe") && !strstr(renderer,"softpipe") && !strstr(renderer,"Software");
    printf("PIXEL=%u,%u,%u,%u GL_ERROR=%x\nRENDER_TEST=%s\nHARDWARE_RENDERING=%s\n", pixel[0],pixel[1],pixel[2],pixel[3],error,rendered?"PASS":"FAIL",hardware?"YES":"NO");
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context); eglDestroySurface(display,surface); eglTerminate(display);
    return rendered && hardware ? 0 : 6;
}
