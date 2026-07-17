package paulscode.android.mupen64plusae.questvr;

import android.app.Activity;
import android.content.Context;
import android.os.SystemClock;
import android.util.Log;
import android.view.View;

/**
 * Thin Java owner for the optional OpenXR presentation path.
 *
 * All native calls except {@link #isDeviceCapable(Context)} must be made from the
 * GameSurface render thread while its EGL context is current.
 */
public final class QuestVrBridge {
    private static final String TAG = "QuestVrBridge";
    private static final String HEAD_TRACKING_FEATURE = "android.hardware.vr.headtracking";
    private static final long WINDOW_READY_TIMEOUT_MILLISECONDS = 1000L;
    private static boolean sLibraryLoaded;
    private static volatile boolean sPresentationLifecycleOwned;
    private static boolean sSourceTextureLogged;

    static {
        try {
            System.loadLibrary("mupen64plus-quest-vr");
            sLibraryLoaded = true;
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "Quest VR native library is unavailable; using normal Android presentation", error);
            sLibraryLoaded = false;
        }
    }

    private QuestVrBridge() {
    }

    public static boolean isDeviceCapable(Context context) {
        return sLibraryLoaded && context.getPackageManager().hasSystemFeature(HEAD_TRACKING_FEATURE);
    }

    /**
     * True while the OpenXR hand-off owns the render-thread lifetime. Quest may destroy the
     * temporary Android SurfaceView after accepting immersive presentation; that must not be
     * interpreted as a request to destroy the OpenXR session.
     */
    public static boolean shouldRetainRenderThreadOnSurfaceLoss() {
        return sPresentationLifecycleOwned;
    }

    /**
     * Give the Activity transition a bounded opportunity to become attached and focused before
     * creating the OpenXR session. The render thread may be started by SurfaceView.surfaceCreated
     * slightly ahead of the final Android window-focus callback during a 2D-to-immersive launch.
     * Waiting forever here would be a deadlock, so a timeout always falls through to OpenXR.
     */
    private static void awaitImmersiveWindow(Activity activity) {
        final long deadline = SystemClock.uptimeMillis() + WINDOW_READY_TIMEOUT_MILLISECONDS;
        boolean attached = false;
        boolean shown = false;
        boolean focused = false;

        do {
            final View decor = activity.getWindow().getDecorView();
            attached = decor != null && decor.isAttachedToWindow() && decor.getWindowToken() != null;
            shown = decor != null && decor.isShown();
            focused = activity.hasWindowFocus();
            if (attached && shown && focused) {
                Log.i(TAG, "Android immersive window is attached, shown, and focused");
                return;
            }
            SystemClock.sleep(16L);
        } while (SystemClock.uptimeMillis() < deadline && !activity.isFinishing() && !activity.isDestroyed());

        Log.w(TAG, "OpenXR window readiness wait expired; continuing with attached=" + attached +
                " shown=" + shown + " focused=" + focused);
    }

    public static boolean initialize(Activity activity) {
        if (!sLibraryLoaded) {
            return false;
        }

        // Claim the render-thread lifetime before the hand-off begins so a racing
        // SurfaceView.surfaceDestroyed callback cannot tear the EGL context down underneath JNI.
        sPresentationLifecycleOwned = true;

        // Queue one real Android buffer while the original window surface is still current. This
        // completes the visible Activity side of Quest's 2D-to-immersive transition.
        if (!nativePrimeAndroidSurface()) {
            Log.w(TAG, "Android bootstrap frame failed; attempting OpenXR initialization anyway");
        }

        // Move the exact EGL context that OpenXR will bind onto a persistent pbuffer BEFORE session
        // creation. Quest may retire the Android window as soon as xrCreateSession is accepted, so
        // parking afterwards leaves a race that looks exactly like a brief flash followed by Home.
        if (!nativeParkEglContext()) {
            Log.e(TAG, "Unable to park EGL before OpenXR session creation; retaining Android presentation");
            sPresentationLifecycleOwned = false;
            return false;
        }
        Log.i(TAG, "OpenXR EGL context parked before session creation");

        awaitImmersiveWindow(activity);
        if (!nativeInitialize(activity)) {
            if (!nativeReleaseParkedEglContext()) {
                Log.w(TAG, "Unable to restore Android presentation after failed OpenXR initialization");
            }
            sPresentationLifecycleOwned = false;
            return false;
        }

        return true;
    }

    public static void setSourceTexture(int texture, int width, int height, boolean requestStereo) {
        if (sLibraryLoaded) {
            if (texture != 0 && !sSourceTextureLogged) {
                Log.i(TAG, "Handing emulator source texture " + texture + " (" + width + "x" + height +
                        ", stereo=" + requestStereo + ") to OpenXR");
                sSourceTextureLogged = true;
            }
            nativeSetSourceTexture(texture, width, height, requestStereo);
        }
    }

    public static void onSourceFrameLatched(long textureTimestampNanos) {
        if (sLibraryLoaded) {
            nativeOnSourceFrameLatched(textureTimestampNanos);
        }
    }

    public static void configure(QuestVrSettings.Configuration configuration) {
        if (sLibraryLoaded) {
            nativeConfigure(configuration.stereoEnabled, configuration.swapEyes, configuration.ipdMeters,
                    configuration.worldUnitsPerMeter, configuration.rotationStrength,
                    configuration.positionEnabled, configuration.maxTranslationMeters,
                    configuration.cameraOffsetXMeters, configuration.cameraOffsetYMeters,
                    configuration.cameraOffsetZMeters, configuration.useOpenXrFov,
                    configuration.marioKartProfileEnabled,
                    configuration.marioKartCameraOffsetYMeters,
                    configuration.marioKartCameraOffsetZMeters, configuration.touchControllerEnabled,
                    configuration.debugLogging);
        }
    }

    public static void recenter() {
        if (sLibraryLoaded) {
            nativeRecenter();
        }
    }

    public static boolean renderFrame() {
        return sLibraryLoaded && nativeRenderFrame();
    }

    public static boolean isExitRequested() {
        return sLibraryLoaded && nativeIsExitRequested();
    }

    public static boolean isStereoSourceActive() {
        return sLibraryLoaded && nativeIsStereoSourceActive();
    }

    public static void shutdown() {
        if (sLibraryLoaded) {
            try {
                nativeShutdown();
            } finally {
                if (!nativeReleaseParkedEglContext()) {
                    Log.w(TAG, "Unable to release the parked OpenXR EGL pbuffer cleanly");
                }
                sPresentationLifecycleOwned = false;
                sSourceTextureLogged = false;
            }
        }
    }

    private static native boolean nativePrimeAndroidSurface();
    private static native boolean nativeParkEglContext();
    private static native boolean nativeReleaseParkedEglContext();
    private static native boolean nativeInitialize(Activity activity);
    private static native void nativeSetSourceTexture(int texture, int width, int height, boolean requestStereo);
    private static native void nativeOnSourceFrameLatched(long textureTimestampNanos);
    private static native void nativeConfigure(boolean stereoEnabled, boolean swapEyes, float ipdMeters,
            float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
            float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
            float cameraOffsetZMeters, boolean useOpenXrFov, boolean marioKartProfileEnabled,
            float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
            boolean touchControllerEnabled, boolean debugLogging);
    private static native void nativeRecenter();
    private static native boolean nativeRenderFrame();
    private static native boolean nativeIsExitRequested();
    private static native boolean nativeIsStereoSourceActive();
    private static native void nativeShutdown();
}
