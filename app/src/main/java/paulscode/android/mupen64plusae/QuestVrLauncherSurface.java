package paulscode.android.mupen64plusae;

import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLExt;
import android.opengl.EGLSurface;
import android.opengl.GLES11Ext;
import android.opengl.GLES30;
import android.os.SystemClock;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import androidx.annotation.NonNull;

import paulscode.android.mupen64plusae.questvr.QuestVrBridge;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/** Owns the launcher EGL/OpenXR thread without involving the emulator renderer. */
final class QuestVrLauncherSurface extends SurfaceView implements SurfaceHolder.Callback {
    static final int MENU_WIDTH = 1600;
    static final int MENU_HEIGHT = 900;
    private static final String TAG = "QuestVrLauncherSurface";
    private static final long REPEAT_DELAY_MS = 360L;
    private static final long REPEAT_INTERVAL_MS = 130L;
    private static final long VR_RESUME_DELAY_MS = 350L;
    private static final long VR_HANDOFF_DELAY_MS = 500L;
    private static final int REPEATABLE_INPUTS = QuestVrBridge.MENU_INPUT_UP |
            QuestVrBridge.MENU_INPUT_DOWN | QuestVrBridge.MENU_INPUT_LEFT |
            QuestVrBridge.MENU_INPUT_RIGHT;

    private final QuestVrLauncherActivity mActivity;
    private final Object mThreadLock = new Object();
    private LauncherThread mThread;
    private boolean mSurfaceAvailable;
    private boolean mDestroying;
    private final Runnable mResumeRunnable = this::resumeVr;

    QuestVrLauncherSurface(QuestVrLauncherActivity activity) {
        super(activity);
        mActivity = activity;
        getHolder().setFormat(PixelFormat.OPAQUE);
        getHolder().addCallback(this);
    }

    @Override
    public void surfaceCreated(@NonNull SurfaceHolder holder) {
        synchronized (mThreadLock) {
            mSurfaceAvailable = true;
        }
        resumeVrAfterDelay();
    }

    @Override
    public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
        // OpenXR swapchain sizing is independent of the temporary Android bootstrap surface.
    }

    @Override
    public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        synchronized (mThreadLock) {
            mSurfaceAvailable = false;
        }
        if (!QuestVrBridge.shouldRetainRenderThreadOnSurfaceLoss() ||
                QuestVrBridge.isExitRequested() || mDestroying) {
            requestStop(null);
        } else {
            QuestVrDiagnostics.info(TAG,
                    "Android launcher surface retired; retaining OpenXR menu thread");
        }
    }

    void resumeVr() {
        synchronized (mThreadLock) {
            if (mDestroying || mActivity.isTransitionPending() || !mSurfaceAvailable ||
                    !getHolder().getSurface().isValid() || (mThread != null && mThread.isAlive())) {
                return;
            }
            mThread = new LauncherThread(getHolder().getSurface());
            mThread.setName("Quest VR launcher");
            mThread.start();
        }
    }

    void resumeVrAfterDelay() {
        removeCallbacks(mResumeRunnable);
        postDelayed(mResumeRunnable, VR_RESUME_DELAY_MS);
    }

    void shutdownForTransition(Runnable completion) {
        requestStop(completion);
    }

    void destroyAndWait() {
        mDestroying = true;
        removeCallbacks(mResumeRunnable);
        final LauncherThread thread;
        synchronized (mThreadLock) {
            thread = mThread;
        }
        requestStop(null);
        if (thread != null && thread != Thread.currentThread()) {
            try {
                thread.join(2500L);
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private void requestStop(Runnable completion) {
        final LauncherThread thread;
        synchronized (mThreadLock) {
            thread = mThread;
            if (thread == null || !thread.isAlive()) {
                if (completion != null) {
                    completion.run();
                }
                return;
            }
            thread.requestStop(completion);
        }
    }

    private final class LauncherThread extends Thread {
        private final Surface mWindow;
        private volatile boolean mStopRequested;
        private Runnable mCompletion;
        private EGLDisplay mDisplay = EGL14.EGL_NO_DISPLAY;
        private EGLContext mContext = EGL14.EGL_NO_CONTEXT;
        private EGLSurface mWindowSurface = EGL14.EGL_NO_SURFACE;
        private SurfaceTexture mMenuTexture;
        private Surface mMenuSurface;
        private int mTextureId;
        private boolean mVrInitialized;

        LauncherThread(Surface window) {
            mWindow = window;
        }

        synchronized void requestStop(Runnable completion) {
            mStopRequested = true;
            if (completion != null) {
                mCompletion = completion;
            }
        }

        @Override
        public void run() {
            boolean initializationFailed = false;
            boolean sessionEnded = false;
            try {
                if (!createEgl() || !createMenuTexture()) {
                    initializationFailed = true;
                    return;
                }
                drawAndLatch(false);
                mVrInitialized = QuestVrBridge.initialize(mActivity);
                if (!mVrInitialized) {
                    initializationFailed = true;
                    return;
                }
                QuestVrBridge.configure(QuestVrSettings.loadLauncher(mActivity));
                QuestVrBridge.setSourceTexture(mTextureId, MENU_WIDTH, MENU_HEIGHT,
                        false, MENU_WIDTH / (float) MENU_HEIGHT);
                drawAndLatch(true);

                int lastRevision = -1;
                int previousInput = 0;
                int heldDirection = 0;
                long nextRepeat = 0L;
                while (!mStopRequested) {
                    final int revision = mActivity.getMenuRevision();
                    if (revision != lastRevision) {
                        if (drawAndLatch(true)) {
                            lastRevision = revision;
                        }
                    }
                    if (!QuestVrBridge.renderFrame()) {
                        if (QuestVrBridge.isExitRequested()) {
                            sessionEnded = !mStopRequested;
                            break;
                        }
                        SystemClock.sleep(10L);
                        continue;
                    }

                    final int input = QuestVrBridge.getMenuInputState();
                    int pressed = input & ~previousInput;
                    final int direction = input & REPEATABLE_INPUTS;
                    final long now = SystemClock.uptimeMillis();
                    if (direction == 0) {
                        heldDirection = 0;
                    } else if (direction != heldDirection) {
                        heldDirection = direction;
                        nextRepeat = now + REPEAT_DELAY_MS;
                    } else if (now >= nextRepeat) {
                        pressed |= direction;
                        nextRepeat = now + REPEAT_INTERVAL_MS;
                    }
                    if (pressed != 0) {
                        mActivity.onVrMenuInput(pressed);
                    }
                    previousInput = input;
                }
            } catch (RuntimeException exception) {
                QuestVrDiagnostics.error(TAG, "VR launcher render thread failed", exception);
                initializationFailed = !mVrInitialized;
                sessionEnded = mVrInitialized && !mStopRequested;
            } finally {
                cleanup();
                final Runnable completion;
                synchronized (this) {
                    completion = mCompletion;
                    mCompletion = null;
                }
                synchronized (mThreadLock) {
                    if (mThread == this) {
                        mThread = null;
                    }
                }
                if (completion != null) {
                    QuestVrDiagnostics.info(TAG,
                            "Launcher OpenXR/EGL teardown complete; waiting " +
                                    VR_HANDOFF_DELAY_MS + "ms before the next activity");
                    SystemClock.sleep(VR_HANDOFF_DELAY_MS);
                    completion.run();
                } else if (initializationFailed) {
                    mActivity.onVrInitializationFailed();
                } else if (sessionEnded) {
                    mActivity.onVrSessionEnded();
                }
            }
        }

        private boolean createEgl() {
            mDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            if (mDisplay == EGL14.EGL_NO_DISPLAY) {
                return false;
            }
            final int[] version = new int[2];
            if (!EGL14.eglInitialize(mDisplay, version, 0, version, 1)) {
                return false;
            }
            final int[] attributes = {
                    EGL14.EGL_RENDERABLE_TYPE, EGLExt.EGL_OPENGL_ES3_BIT_KHR,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT | EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_RED_SIZE, 8,
                    EGL14.EGL_GREEN_SIZE, 8,
                    EGL14.EGL_BLUE_SIZE, 8,
                    EGL14.EGL_ALPHA_SIZE, 8,
                    EGL14.EGL_DEPTH_SIZE, 0,
                    EGL14.EGL_NONE
            };
            final EGLConfig[] configs = new EGLConfig[1];
            final int[] count = new int[1];
            if (!EGL14.eglChooseConfig(mDisplay, attributes, 0, configs, 0, 1, count, 0) ||
                    count[0] == 0) {
                return false;
            }
            final int[] contextAttributes = {
                    EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE
            };
            mContext = EGL14.eglCreateContext(mDisplay, configs[0], EGL14.EGL_NO_CONTEXT,
                    contextAttributes, 0);
            if (mContext == EGL14.EGL_NO_CONTEXT) {
                return false;
            }
            mWindowSurface = EGL14.eglCreateWindowSurface(mDisplay, configs[0], mWindow,
                    new int[]{EGL14.EGL_NONE}, 0);
            if (mWindowSurface == EGL14.EGL_NO_SURFACE ||
                    !EGL14.eglMakeCurrent(mDisplay, mWindowSurface, mWindowSurface, mContext)) {
                return false;
            }
            GLES30.glViewport(0, 0, Math.max(1, getWidth()), Math.max(1, getHeight()));
            GLES30.glClearColor(0.0f, 0.02f, 0.06f, 1.0f);
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT);
            EGL14.eglSwapBuffers(mDisplay, mWindowSurface);
            return true;
        }

        private boolean createMenuTexture() {
            final int[] textures = new int[1];
            GLES30.glGenTextures(1, textures, 0);
            mTextureId = textures[0];
            if (mTextureId == 0) {
                return false;
            }
            GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, mTextureId);
            GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                    GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                    GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR);
            GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                    GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE);
            GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES,
                    GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE);
            mMenuTexture = new SurfaceTexture(mTextureId);
            mMenuTexture.setDefaultBufferSize(MENU_WIDTH, MENU_HEIGHT);
            mMenuSurface = new Surface(mMenuTexture);
            return true;
        }

        private boolean drawAndLatch(boolean notifyBridge) {
            if (mMenuSurface == null || !mMenuSurface.isValid()) {
                return false;
            }
            Canvas canvas = null;
            try {
                canvas = mMenuSurface.lockCanvas(null);
                mActivity.drawVrMenu(canvas);
            } finally {
                if (canvas != null) {
                    mMenuSurface.unlockCanvasAndPost(canvas);
                }
            }
            try {
                mMenuTexture.updateTexImage();
                if (notifyBridge) {
                    final float[] transform = new float[16];
                    mMenuTexture.getTransformMatrix(transform);
                    QuestVrBridge.onSourceFrameLatched(mMenuTexture.getTimestamp(), transform);
                }
                return true;
            } catch (IllegalStateException exception) {
                QuestVrDiagnostics.warn(TAG, "Menu SurfaceTexture latch deferred: " + exception);
                return false;
            }
        }

        private void cleanup() {
            if (mVrInitialized) {
                QuestVrBridge.setSourceTexture(0, 0, 0, false, 16.0f / 9.0f);
                QuestVrBridge.shutdown();
                mVrInitialized = false;
            }
            if (mMenuSurface != null) {
                mMenuSurface.release();
                mMenuSurface = null;
            }
            if (mMenuTexture != null) {
                mMenuTexture.release();
                mMenuTexture = null;
            }
            if (mDisplay != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(mDisplay, EGL14.EGL_NO_SURFACE,
                        EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
                if (mWindowSurface != EGL14.EGL_NO_SURFACE) {
                    EGL14.eglDestroySurface(mDisplay, mWindowSurface);
                }
                if (mContext != EGL14.EGL_NO_CONTEXT) {
                    EGL14.eglDestroyContext(mDisplay, mContext);
                }
                EGL14.eglTerminate(mDisplay);
            }
            mWindowSurface = EGL14.EGL_NO_SURFACE;
            mContext = EGL14.EGL_NO_CONTEXT;
            mDisplay = EGL14.EGL_NO_DISPLAY;
        }
    }
}
