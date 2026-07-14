package paulscode.android.mupen64plusae.questvr;

import android.app.Activity;
import android.content.Context;
import android.util.Log;

/**
 * Thin Java owner for the optional OpenXR presentation path.
 *
 * All native calls except {@link #isDeviceCapable(Context)} must be made from the
 * GameSurface render thread while its EGL context is current.
 */
public final class QuestVrBridge {
    private static final String TAG = "QuestVrBridge";
    private static final String HEAD_TRACKING_FEATURE = "android.hardware.vr.headtracking";
    private static boolean sLibraryLoaded;

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

    public static boolean initialize(Activity activity) {
        return sLibraryLoaded && nativeInitialize(activity);
    }

    public static void setSourceTexture(int texture, int width, int height, boolean requestStereo) {
        if (sLibraryLoaded) {
            nativeSetSourceTexture(texture, width, height, requestStereo);
        }
    }

    public static boolean renderFrame() {
        return sLibraryLoaded && nativeRenderFrame();
    }

    public static boolean isStereoSourceActive() {
        return sLibraryLoaded && nativeIsStereoSourceActive();
    }

    public static void shutdown() {
        if (sLibraryLoaded) {
            nativeShutdown();
        }
    }

    private static native boolean nativeInitialize(Activity activity);
    private static native void nativeSetSourceTexture(int texture, int width, int height, boolean requestStereo);
    private static native boolean nativeRenderFrame();
    private static native boolean nativeIsStereoSourceActive();
    private static native void nativeShutdown();
}
