package paulscode.android.mupen64plusae.questvr;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.os.Build;
import android.os.SystemClock;
import android.view.View;

import java.io.File;
import java.nio.ByteBuffer;
import java.util.Locale;

/**
 * Thin Java owner for the optional OpenXR presentation path.
 *
 * All native calls except the startup capability accessors must be made from the
 * GameSurface render thread while its EGL context is current.
 */
public final class QuestVrBridge {
    private static final String TAG = "QuestVrBridge";
    private static final String HEAD_TRACKING_FEATURE = "android.hardware.vr.headtracking";
    private static final long WINDOW_READY_TIMEOUT_MILLISECONDS = 1000L;
    private static boolean sLibraryLoaded;
    private static volatile boolean sPresentationLifecycleOwned;
    private static boolean sSourceTextureLogged;
    private static int sSourceWidth;
    private static int sSourceHeight;

    public static final int MENU_INPUT_UP = 1 << 0;
    public static final int MENU_INPUT_DOWN = 1 << 1;
    public static final int MENU_INPUT_LEFT = 1 << 2;
    public static final int MENU_INPUT_RIGHT = 1 << 3;
    public static final int MENU_INPUT_SELECT = 1 << 4;
    public static final int MENU_INPUT_BACK = 1 << 5;

    static {
        try {
            System.loadLibrary("mupen64plus-quest-vr");
            sLibraryLoaded = true;
            QuestVrDiagnostics.info(TAG, "Loaded native library libmupen64plus-quest-vr.so");
        } catch (UnsatisfiedLinkError error) {
            QuestVrDiagnostics.error(TAG,
                    "Quest VR native library is unavailable; using normal Android presentation",
                    error);
            sLibraryLoaded = false;
        }
    }

    private QuestVrBridge() {
    }

    public static boolean isNativeLibraryLoaded() {
        return sLibraryLoaded;
    }

    public static boolean hasHeadTrackingFeature(Context context) {
        return context.getPackageManager().hasSystemFeature(HEAD_TRACKING_FEATURE);
    }

    /**
     * Whether this APK can attempt the native OpenXR path.
     *
     * The Android head-tracking feature is deliberately advisory. Quest firmware/app packaging
     * combinations can omit that PackageManager feature even though the OpenXR runtime is present.
     * The loader/instance/system/session calls are the authoritative availability test.
     */
    public static boolean isDeviceCapable(Context context) {
        return sLibraryLoaded;
    }

    /**
     * Keep the VR-first shell specific to headset hardware. The Quest system feature is advisory
     * and is absent on some current firmware, so the manufacturer/model check covers that known
     * runtime while ordinary Android devices continue directly to the flat gallery.
     */
    public static boolean shouldLaunchVrLauncher(Context context) {
        if (!sLibraryLoaded || !context.getSharedPreferences(
                QuestVrSettings.PREFERENCES_NAME, Context.MODE_PRIVATE)
                .getBoolean("vr_launcher_enabled", true)) {
            return false;
        }
        final String manufacturer = Build.MANUFACTURER == null ? "" : Build.MANUFACTURER;
        final String model = Build.MODEL == null ? "" : Build.MODEL;
        return hasHeadTrackingFeature(context) ||
                "oculus".equals(manufacturer.toLowerCase(Locale.US)) ||
                "meta".equals(manufacturer.toLowerCase(Locale.US)) ||
                model.toLowerCase(Locale.US).contains("quest");
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
                QuestVrDiagnostics.info(TAG,
                        "Android immersive window is attached, shown, and focused");
                return;
            }
            SystemClock.sleep(16L);
        } while (SystemClock.uptimeMillis() < deadline && !activity.isFinishing() && !activity.isDestroyed());

        QuestVrDiagnostics.warn(TAG,
                "OpenXR window readiness wait expired; continuing with attached=" + attached +
                        " shown=" + shown + " focused=" + focused);
    }

    public static boolean initialize(Activity activity) {
        if (!sLibraryLoaded) {
            QuestVrDiagnostics.error(TAG,
                    "OpenXR initialization skipped because the native bridge library did not load",
                    null);
            return false;
        }

        final File diagnosticLog = QuestVrDiagnostics.getLatestLogFile(activity);
        nativeConfigureDiagnosticLog(diagnosticLog.getAbsolutePath());
        QuestVrDiagnostics.info(TAG, "initialize entry activity=" + activity.getClass().getName() +
                " finishing=" + activity.isFinishing() + " destroyed=" + activity.isDestroyed() +
                " nativeLibraryLoaded=" + sLibraryLoaded + " headTrackingFeature=" +
                hasHeadTrackingFeature(activity));

        // Claim the render-thread lifetime before the hand-off begins so a racing
        // SurfaceView.surfaceDestroyed callback cannot tear the EGL context down underneath JNI.
        sPresentationLifecycleOwned = true;

        // Queue one real Android buffer while the original window surface is still current. This
        // completes the visible Activity side of Quest's 2D-to-immersive transition.
        if (!nativePrimeAndroidSurface()) {
            QuestVrDiagnostics.warn(TAG,
                    "Android bootstrap frame failed; attempting OpenXR initialization anyway");
        }

        // Move the exact EGL context that OpenXR will bind onto a persistent pbuffer BEFORE session
        // creation. Quest may retire the Android window as soon as xrCreateSession is accepted, so
        // parking afterwards leaves a race that looks exactly like a brief flash followed by Home.
        if (!nativeParkEglContext()) {
            QuestVrDiagnostics.error(TAG,
                    "Unable to park EGL before OpenXR session creation; retaining Android presentation",
                    null);
            sPresentationLifecycleOwned = false;
            return false;
        }
        QuestVrDiagnostics.info(TAG, "OpenXR EGL context parked before session creation");

        awaitImmersiveWindow(activity);
        if (!nativeInitialize(activity)) {
            if (!nativeReleaseParkedEglContext()) {
                QuestVrDiagnostics.warn(TAG,
                        "Unable to restore Android presentation after failed OpenXR initialization");
            }
            sPresentationLifecycleOwned = false;
            return false;
        }

        return true;
    }

    public static void setSourceTexture(int texture, int width, int height, boolean requestStereo,
            float contentAspect) {
        if (sLibraryLoaded) {
            if (texture != 0 && !sSourceTextureLogged) {
                QuestVrDiagnostics.info(TAG, "Handing emulator source texture " + texture +
                        " (" + width + "x" + height + ", stereo=" + requestStereo +
                        ", contentAspect=" + contentAspect + ") to OpenXR");
                sSourceTextureLogged = true;
            }
            nativeSetSourceTexture(texture, width, height, requestStereo, contentAspect);
            sSourceWidth = texture != 0 ? width : 0;
            sSourceHeight = texture != 0 ? height : 0;
        }
    }

    public static void onSourceFrameLatched(long textureTimestampNanos,
            float[] surfaceTransformMatrix) {
        if (sLibraryLoaded) {
            nativeOnSourceFrameLatched(textureTimestampNanos, surfaceTransformMatrix);
        }
    }

    /**
     * Capture the complete SurfaceTexture image before OpenXR eye splitting and aspect fitting.
     * This is deliberately user-triggered through the existing screenshot action, so it cannot
     * generate an unbounded stream of large diagnostic files.
     */
    public static Bitmap captureSourceFrame() {
        if (!sLibraryLoaded || sSourceWidth <= 0 || sSourceHeight <= 0) {
            return null;
        }
        final byte[] rgba = nativeCaptureSourceFrame();
        final long expectedSize = (long) sSourceWidth * sSourceHeight * 4L;
        if (rgba == null || expectedSize > Integer.MAX_VALUE || rgba.length != expectedSize) {
            QuestVrDiagnostics.warn(TAG, "Pre-OpenXR source capture failed or had an " +
                    "unexpected size: expected=" + expectedSize + " actual=" +
                    (rgba == null ? -1 : rgba.length));
            return null;
        }
        // GLideN64 often leaves the Android surface alpha channel unset. The source is composed
        // opaquely in OpenXR, so make the saved PNG represent what the headset actually sees.
        for (int index = 3; index < rgba.length; index += 4) {
            rgba[index] = (byte) 0xFF;
        }
        final Bitmap bitmap = Bitmap.createBitmap(sSourceWidth, sSourceHeight,
                Bitmap.Config.ARGB_8888);
        bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(rgba));
        QuestVrDiagnostics.info(TAG, "Captured pre-OpenXR packed source " +
                sSourceWidth + "x" + sSourceHeight);
        return bitmap;
    }

    public static void configure(QuestVrSettings.Configuration configuration) {
        if (sLibraryLoaded) {
            // Quest hardware validation of the folded Mario Kart camera path showed that its
            // rotation convention is opposite the older late-clip path. Keep the user-facing
            // setting intuitive: +1 follows the headset and negative values intentionally invert.
            final float nativeRotationStrength = -configuration.rotationStrength;
            QuestVrDiagnostics.info(TAG, "Effective presentation mode=" +
                    configuration.presentationModeName + " immersiveViewScale=" +
                    configuration.immersiveViewScale + " screenScale=" +
                    configuration.screenScale + " screenDistance=" +
                    configuration.screenDistanceMeters + "m startupProofLayers=" +
                    configuration.startupProofLayers + " stereo=" +
                    configuration.stereoEnabled + " rotation=" +
                    configuration.rotationStrength + " nativeRotation=" + nativeRotationStrength);
            nativeConfigure(configuration.stereoEnabled, configuration.swapEyes, configuration.ipdMeters,
                    configuration.presentationMode, configuration.immersiveViewScale,
                    configuration.screenScale, configuration.screenDistanceMeters,
                    configuration.startupProofLayers,
                    configuration.worldUnitsPerMeter, nativeRotationStrength,
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

    /** Current debounced-by-the-caller Touch state for the VR launcher. */
    public static int getMenuInputState() {
        return sLibraryLoaded ? nativeGetMenuInputState() : 0;
    }

    public static boolean isExitRequested() {
        return sLibraryLoaded && nativeIsExitRequested();
    }

    public static boolean isStereoSourceActive() {
        return sLibraryLoaded && nativeIsStereoSourceActive();
    }

    public static void shutdown() {
        if (sLibraryLoaded) {
            QuestVrDiagnostics.info(TAG, "shutdown entry");
            try {
                nativeShutdown();
            } finally {
                if (!nativeReleaseParkedEglContext()) {
                    QuestVrDiagnostics.warn(TAG,
                            "Unable to release the parked OpenXR EGL pbuffer cleanly");
                }
                sPresentationLifecycleOwned = false;
                sSourceTextureLogged = false;
                sSourceWidth = 0;
                sSourceHeight = 0;
            }
        }
    }

    private static native boolean nativePrimeAndroidSurface();
    private static native void nativeConfigureDiagnosticLog(String path);
    private static native boolean nativeParkEglContext();
    private static native boolean nativeReleaseParkedEglContext();
    private static native boolean nativeInitialize(Activity activity);
    private static native void nativeSetSourceTexture(int texture, int width, int height,
            boolean requestStereo, float contentAspect);
    private static native void nativeOnSourceFrameLatched(long textureTimestampNanos,
            float[] surfaceTransformMatrix);
    private static native byte[] nativeCaptureSourceFrame();
    private static native void nativeConfigure(boolean stereoEnabled, boolean swapEyes, float ipdMeters,
            int presentationMode, float immersiveViewScale,
            float screenScale, float screenDistanceMeters, boolean startupProofLayers,
            float worldUnitsPerMeter, float rotationStrength, boolean positionEnabled,
            float maxTranslationMeters, float cameraOffsetXMeters, float cameraOffsetYMeters,
            float cameraOffsetZMeters, boolean useOpenXrFov, boolean marioKartProfileEnabled,
            float marioKartCameraOffsetYMeters, float marioKartCameraOffsetZMeters,
            boolean touchControllerEnabled, boolean debugLogging);
    private static native void nativeRecenter();
    private static native boolean nativeRenderFrame();
    private static native int nativeGetMenuInputState();
    private static native boolean nativeIsExitRequested();
    private static native boolean nativeIsStereoSourceActive();
    private static native void nativeShutdown();
}
