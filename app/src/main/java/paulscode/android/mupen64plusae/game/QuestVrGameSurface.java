package paulscode.android.mupen64plusae.game;

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

    @Override
    protected void stopGlContext() {
        // GameSurface historically only stops while the Android Surface is marked available. A VR
        // hand-off deliberately clears that flag, but Activity.onStop must still be able to perform
        // the final orderly OpenXR/EGL shutdown.
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
