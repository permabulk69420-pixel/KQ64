package paulscode.android.mupen64plusae.game;

import android.app.Activity;
import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;

import paulscode.android.mupen64plusae.questvr.QuestVrBridge;

/**
 * GameSurface variant that separates the Quest OpenXR renderer lifetime from the temporary
 * Android SurfaceView window used to enter immersive mode.
 */
public final class QuestVrGameSurface extends GameSurface {
    private static final String TAG = "QuestVrGameSurface";

    public QuestVrGameSurface(Context context, AttributeSet attributes) {
        super(context, attributes);
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        if (QuestVrBridge.shouldRetainRenderThreadOnSurfaceLoss()) {
            // Once OpenXR owns presentation, Quest may retire the Android window Surface as part of
            // the 2D-to-immersive transition. The GL context has already been moved to a pbuffer, so
            // stopping the render thread here would immediately destroy the accepted XR session.
            Log.i(TAG, "Android window surface retired; retaining OpenXR render thread");
            mSurfaceAvailable = false;
            return;
        }
        super.surfaceDestroyed(holder);
    }

    private boolean isTransientVrActivityStop() {
        if (!QuestVrBridge.shouldRetainRenderThreadOnSurfaceLoss() ||
                QuestVrBridge.isExitRequested()) {
            return false;
        }
        if (!(mContext instanceof Activity)) {
            return true;
        }
        final Activity activity = (Activity) mContext;
        return !activity.isFinishing() && !activity.isDestroyed();
    }

    @Override
    protected void stopGlContext() {
        // The normal GameActivity stops its render thread from Activity.onStop. Quest can issue a
        // transient Android onStop while the accepted OpenXR session is still responsible for the
        // headset. Treating that callback as final shutdown produces one compositor flash followed
        // by an immediate return to Home. A real user/app exit sets isFinishing/isDestroyed or the
        // native runtime exit flag, and still takes the ordinary cleanup path below.
        if (isTransientVrActivityStop()) {
            Log.i(TAG, "Ignoring transient Activity stop while OpenXR owns presentation");
            return;
        }

        // GameSurface historically only stops while the Android Surface is marked available. A VR
        // hand-off deliberately clears that flag, but a final Activity stop must still perform the
        // orderly OpenXR/EGL shutdown.
        final boolean restoreSurfaceFlag = mGlContextStarted && !mSurfaceAvailable;
        if (restoreSurfaceFlag) {
            mSurfaceAvailable = true;
        }
        try {
            super.stopGlContext();
        } finally {
            if (restoreSurfaceFlag) {
                mSurfaceAvailable = false;
            }
        }
    }
}
