#include <jni.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstring>

namespace {

constexpr const char* TAG = "M64P-QuestVR";

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

EGLDisplay gParkingDisplay = EGL_NO_DISPLAY;
EGLContext gParkingContext = EGL_NO_CONTEXT;
EGLSurface gParkingSurface = EGL_NO_SURFACE;
EGLSurface gParkingWindowSurface = EGL_NO_SURFACE;
bool gParkingActive = false;
bool gParkingSurfaceless = false;

void clearParkingState() {
    gParkingDisplay = EGL_NO_DISPLAY;
    gParkingContext = EGL_NO_CONTEXT;
    gParkingSurface = EGL_NO_SURFACE;
    gParkingWindowSurface = EGL_NO_SURFACE;
    gParkingActive = false;
    gParkingSurfaceless = false;
}

bool supportsSurfacelessContext(EGLDisplay display) {
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    return extensions != nullptr &&
            std::strstr(extensions, "EGL_KHR_surfaceless_context") != nullptr;
}

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

/**
 * Detach the OpenXR graphics-binding context from the Android window before session creation.
 *
 * A pbuffer is preferred because it gives the context a small conventional draw surface. If the
 * Android-selected EGLConfig does not expose EGL_PBUFFER_BIT, Quest can instead keep the context
 * current with EGL_KHR_surfaceless_context. OpenXR only renders into its own swapchain FBOs, so the
 * Android window is not needed after the bootstrap frame.
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeParkEglContext(
        JNIEnv*, jclass) {
    if (gParkingActive) {
        return JNI_TRUE;
    }

    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface windowSurface = eglGetCurrentSurface(EGL_DRAW);
    if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT || windowSurface == EGL_NO_SURFACE) {
        LOGE("Cannot park OpenXR EGL context: no current display/context/window surface");
        return JNI_FALSE;
    }

    EGLint configId = 0;
    if (eglQueryContext(display, context, EGL_CONFIG_ID, &configId) != EGL_TRUE) {
        LOGE("Cannot park OpenXR EGL context: eglQueryContext failed (error=0x%x)", eglGetError());
        return JNI_FALSE;
    }

    const EGLint configAttributes[] = {EGL_CONFIG_ID, configId, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (eglChooseConfig(display, configAttributes, &config, 1, &configCount) != EGL_TRUE ||
            configCount != 1 || config == nullptr) {
        LOGE("Cannot park OpenXR EGL context: unable to resolve EGLConfig %d (error=0x%x)",
             configId, eglGetError());
        return JNI_FALSE;
    }

    EGLSurface parkingSurface = EGL_NO_SURFACE;
    EGLint surfaceType = 0;
    const bool supportsPbuffer =
            eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &surfaceType) == EGL_TRUE &&
            (surfaceType & EGL_PBUFFER_BIT) != 0;
    if (supportsPbuffer) {
        const EGLint pbufferAttributes[] = {
            EGL_WIDTH, 16,
            EGL_HEIGHT, 16,
            EGL_NONE,
        };
        parkingSurface = eglCreatePbufferSurface(display, config, pbufferAttributes);
        if (parkingSurface != EGL_NO_SURFACE &&
                eglMakeCurrent(display, parkingSurface, parkingSurface, context) == EGL_TRUE) {
            gParkingDisplay = display;
            gParkingContext = context;
            gParkingSurface = parkingSurface;
            gParkingWindowSurface = windowSurface;
            gParkingActive = true;
            gParkingSurfaceless = false;
            LOGI("Moved OpenXR EGL context %p from Android window %p to pbuffer %p (config=%d)",
                 context, windowSurface, parkingSurface, configId);
            return JNI_TRUE;
        }

        const EGLint pbufferError = eglGetError();
        if (parkingSurface != EGL_NO_SURFACE) {
            eglDestroySurface(display, parkingSurface);
            parkingSurface = EGL_NO_SURFACE;
        }
        // The failed make-current may have disturbed the binding; restore it before trying the
        // surfaceless path or returning to ordinary Android presentation.
        eglMakeCurrent(display, windowSurface, windowSurface, context);
        LOGW("Pbuffer parking failed for EGLConfig %d (error=0x%x); trying surfaceless EGL",
             configId, pbufferError);
    } else {
        LOGW("EGLConfig %d lacks EGL_PBUFFER_BIT (type=0x%x); trying surfaceless EGL",
             configId, surfaceType);
    }

    if (supportsSurfacelessContext(display) &&
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_TRUE) {
        gParkingDisplay = display;
        gParkingContext = context;
        gParkingSurface = EGL_NO_SURFACE;
        gParkingWindowSurface = windowSurface;
        gParkingActive = true;
        gParkingSurfaceless = true;
        LOGI("Detached OpenXR EGL context %p from Android window %p using surfaceless EGL (config=%d)",
             context, windowSurface, configId);
        return JNI_TRUE;
    }

    const EGLint surfacelessError = eglGetError();
    eglMakeCurrent(display, windowSurface, windowSurface, context);
    LOGE("Cannot detach OpenXR EGL context: pbuffer unavailable and surfaceless EGL failed (error=0x%x)",
         surfacelessError);
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeReleaseParkedEglContext(
        JNIEnv*, jclass) {
    if (!gParkingActive) {
        return JNI_TRUE;
    }

    bool success = true;
    const EGLSurface expectedSurface = gParkingSurfaceless ? EGL_NO_SURFACE : gParkingSurface;
    const bool parkingContextCurrent = eglGetCurrentDisplay() == gParkingDisplay &&
            eglGetCurrentContext() == gParkingContext &&
            eglGetCurrentSurface(EGL_DRAW) == expectedSurface;

    bool restoredWindow = false;
    if (parkingContextCurrent && gParkingWindowSurface != EGL_NO_SURFACE) {
        if (eglMakeCurrent(gParkingDisplay, gParkingWindowSurface, gParkingWindowSurface,
                gParkingContext) == EGL_TRUE) {
            restoredWindow = true;
            LOGI("Restored EGL context to Android window after OpenXR parking");
        } else {
            LOGW("Android window could not be restored after OpenXR parking (error=0x%x)",
                 eglGetError());
        }
    }

    if (parkingContextCurrent && !restoredWindow) {
        if (eglMakeCurrent(gParkingDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                EGL_NO_CONTEXT) != EGL_TRUE) {
            LOGE("Failed to unbind parked OpenXR EGL context (error=0x%x)", eglGetError());
            success = false;
        }
    }

    if (gParkingSurface != EGL_NO_SURFACE) {
        if (eglDestroySurface(gParkingDisplay, gParkingSurface) != EGL_TRUE) {
            LOGE("Failed to destroy parked OpenXR EGL pbuffer (error=0x%x)", eglGetError());
            success = false;
        } else {
            LOGI("Released parked OpenXR EGL pbuffer");
        }
    } else {
        LOGI("Released surfaceless OpenXR EGL parking state");
    }

    clearParkingState();
    return success ? JNI_TRUE : JNI_FALSE;
}
