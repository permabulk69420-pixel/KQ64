package paulscode.android.mupen64plusae;

import android.graphics.Canvas;
import android.view.MotionEvent;
import android.view.View;

import androidx.annotation.NonNull;

/**
 * Android-panel surface for the Quest launcher.
 *
 * The launcher deliberately stays out of OpenXR. Quest can present this ordinary Android window
 * as a floating system panel, while GameActivity remains the only owner of an immersive OpenXR
 * instance. Keeping the existing Canvas renderer preserves the launcher's visual design without
 * creating a second compositor session before a game starts.
 */
final class QuestVrLauncherSurface extends View {
    static final int MENU_WIDTH = 1600;
    static final int MENU_HEIGHT = 900;
    private static final float SWIPE_THRESHOLD = 48.0f;

    private final QuestVrLauncherActivity mActivity;
    private float mPointerDownX;
    private float mPointerDownY;

    QuestVrLauncherSurface(QuestVrLauncherActivity activity) {
        super(activity);
        mActivity = activity;
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);
        mActivity.drawVrMenu(canvas);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                mPointerDownX = event.getX();
                mPointerDownY = event.getY();
                requestFocus();
                return true;
            case MotionEvent.ACTION_UP:
                performClick();
                final float deltaX = event.getX() - mPointerDownX;
                final float deltaY = event.getY() - mPointerDownY;
                final float menuY = toMenuY(event.getY());
                if (Math.abs(deltaX) >= SWIPE_THRESHOLD &&
                        Math.abs(deltaX) > Math.abs(deltaY) && menuY < 570.0f) {
                    mActivity.onPointerLibrarySwipe(deltaX < 0.0f ? 1 : -1);
                } else {
                    mActivity.onPointerTap(toMenuX(event.getX()), menuY);
                }
                return true;
            case MotionEvent.ACTION_CANCEL:
                return true;
            default:
                return true;
        }
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    void refresh() {
        postInvalidateOnAnimation();
    }

    void resumePanel() {
        requestFocus();
        refresh();
    }

    void destroyAndWait() {
        // There is no launcher render thread or OpenXR instance to tear down.
    }

    private float toMenuX(float viewX) {
        return getWidth() == 0 ? 0.0f : viewX * MENU_WIDTH / getWidth();
    }

    private float toMenuY(float viewY) {
        return getHeight() == 0 ? 0.0f : viewY * MENU_HEIGHT / getHeight();
    }
}
