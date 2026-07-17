#include <jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

namespace {

constexpr const char* TAG = "M64P-QuestVR";

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

}  // namespace

/**
 * Present one ordinary Android window frame before the OpenXR session is created.
 *
 * Quest's Android/OpenXR hand-off still starts from a resumed, visible Activity window. The normal
 * emulator presenter previously created its EGL window surface but never swapped it in VR mode,
 * because all later frames went directly to OpenXR swapchains. That left the runtime waiting on the
 * three-dot transition while the native session was otherwise alive.
 *
 * This method is called on GameSurface's render thread while its EGL context and window surface are
 * current. It deliberately does not create or own any EGL objects.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativePrimeAndroidSurface(
        JNIEnv*, jclass) {
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE) {
        LOGE("Cannot prime Android window before OpenXR: no current EGL display/context/surface");
        return JNI_FALSE;
    }

    EGLint width = 0;
    EGLint height = 0;
    if (eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
            eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE ||
            width <= 0 || height <= 0) {
        LOGE("Cannot prime Android window before OpenXR: invalid EGL surface dimensions (error=0x%x)",
             eglGetError());
        return JNI_FALSE;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    while (glGetError() != GL_NO_ERROR) {
        // Discard stale errors so the bootstrap result is unambiguous in logcat.
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    const GLenum glError = glGetError();
    const EGLBoolean swapped = glError == GL_NO_ERROR ? eglSwapBuffers(display, surface) : EGL_FALSE;
    const EGLint eglError = swapped == EGL_TRUE ? EGL_SUCCESS : eglGetError();

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glClearColor(previousClearColor[0], previousClearColor[1],
                 previousClearColor[2], previousClearColor[3]);
    if (scissorWasEnabled == GL_TRUE) {
        glEnable(GL_SCISSOR_TEST);
    }

    if (glError != GL_NO_ERROR || swapped != EGL_TRUE) {
        LOGE("Android window bootstrap frame failed: gl=0x%x egl=0x%x", glError, eglError);
        return JNI_FALSE;
    }

    LOGI("Presented Android bootstrap frame (%dx%d) before OpenXR session creation", width, height);
    return JNI_TRUE;
}
