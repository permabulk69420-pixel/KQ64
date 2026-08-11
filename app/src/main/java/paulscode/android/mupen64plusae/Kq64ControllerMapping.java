package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.PorterDuff;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.os.SystemClock;
import android.text.TextUtils;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.preference.PreferenceManager;

import java.io.File;

import paulscode.android.mupen64plusae.input.AbstractController;
import paulscode.android.mupen64plusae.input.map.InputMap;
import paulscode.android.mupen64plusae.input.provider.AbstractProvider;
import paulscode.android.mupen64plusae.persistent.AppData;
import paulscode.android.mupen64plusae.persistent.ConfigFile;
import paulscode.android.mupen64plusae.persistent.ConfigFile.ConfigSection;
import paulscode.android.mupen64plusae.persistent.GlobalPrefs;
import paulscode.android.mupen64plusae.profile.ControllerProfile;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;

/** Quest-native controller mapping launched from the KQ64 front door. */
final class Kq64ControllerMapping {
    private static final String TAG = "Kq64Controls";
    private static final String OVERLAY_TAG = "kq64-controls-surface";

    private Kq64ControllerMapping() {
    }

    static void installLauncherButton(@NonNull AppCompatActivity activity) {
        final View root = activity.findViewById(android.R.id.content);
        if (!(root instanceof ViewGroup)) {
            QuestVrDiagnostics.warn(TAG, "Launcher content root unavailable");
            return;
        }

        final ViewGroup content = (ViewGroup) root;
        final View previous = content.findViewWithTag(OVERLAY_TAG);
        if (previous != null) {
            content.removeView(previous);
        }

        final ControlsSurface controls = new ControlsSurface(activity);
        controls.setTag(OVERLAY_TAG);
        content.addView(controls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        controls.bringToFront();
        QuestVrDiagnostics.info(TAG,
                "Installed Z-ordered launcher Controls surface above QuestVrLauncherSurface");
    }

    static void show(@NonNull Activity activity) {
        new MappingDialog(activity).show();
    }

    /**
     * SurfaceView is intentional. The launcher itself is a SurfaceView, so a normal sibling View can
     * receive touches while remaining visually hidden behind the launcher surface on Quest.
     */
    private static final class ControlsSurface extends SurfaceView
            implements SurfaceHolder.Callback {
        private static final float DESIGN_WIDTH = 1600.0f;
        private static final float DESIGN_HEIGHT = 900.0f;
        private static final RectF BUTTON_RECT = new RectF(1310, 710, 1525, 790);

        private final Activity mActivity;
        private boolean mPressed;

        ControlsSurface(@NonNull Activity activity) {
            super(activity);
            mActivity = activity;
            setZOrderOnTop(true);
            getHolder().setFormat(PixelFormat.TRANSLUCENT);
            getHolder().addCallback(this);
            setFocusable(false);
            setClickable(true);
            setBackgroundColor(Color.TRANSPARENT);
        }

        @Override
        public void surfaceCreated(@NonNull SurfaceHolder holder) {
            drawSurface();
        }

        @Override
        public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
            drawSurface();
        }

        @Override
        public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
        }

        private void drawSurface() {
            final SurfaceHolder holder = getHolder();
            Canvas canvas = null;
            try {
                canvas = holder.lockCanvas();
                if (canvas == null) {
                    return;
                }
                canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR);
                final float sx = getWidth() / DESIGN_WIDTH;
                final float sy = getHeight() / DESIGN_HEIGHT;
                canvas.save();
                canvas.scale(sx, sy);

                final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
                paint.setStyle(Paint.Style.FILL);
                paint.setShader(new LinearGradient(
                        BUTTON_RECT.left, BUTTON_RECT.top,
                        BUTTON_RECT.right, BUTTON_RECT.bottom,
                        mPressed ? Color.argb(250, 10, 71, 116) : Color.argb(242, 8, 48, 83),
                        Color.argb(245, 3, 13, 31), Shader.TileMode.CLAMP));
                canvas.drawRoundRect(BUTTON_RECT, 25, 25, paint);
                paint.setShader(null);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(mPressed ? 5 : 3);
                paint.setColor(mPressed ? Color.rgb(53, 211, 255) :
                        Color.argb(220, 19, 158, 222));
                canvas.drawRoundRect(BUTTON_RECT, 25, 25, paint);
                paint.setStyle(Paint.Style.FILL);
                MappingView.drawText(canvas, paint, "CONTROLS", BUTTON_RECT.centerX(),
                        BUTTON_RECT.centerY() + 7, 18, Color.WHITE,
                        Paint.Align.CENTER, true);
                canvas.restore();
            } catch (RuntimeException exception) {
                QuestVrDiagnostics.warn(TAG, "Unable to draw Controls surface: " + exception);
            } finally {
                if (canvas != null) {
                    holder.unlockCanvasAndPost(canvas);
                }
            }
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            final float x = event.getX() * DESIGN_WIDTH / Math.max(1, getWidth());
            final float y = event.getY() * DESIGN_HEIGHT / Math.max(1, getHeight());
            final boolean inside = BUTTON_RECT.contains(x, y);

            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    if (!inside) {
                        return false;
                    }
                    mPressed = true;
                    drawSurface();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (!mPressed) {
                        return false;
                    }
                    mPressed = inside;
                    drawSurface();
                    return true;
                case MotionEvent.ACTION_UP:
                    if (!mPressed) {
                        return false;
                    }
                    mPressed = false;
                    drawSurface();
                    if (inside) {
                        performClick();
                        show(mActivity);
                    }
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    if (mPressed) {
                        mPressed = false;
                        drawSurface();
                        return true;
                    }
                    return false;
                default:
                    return mPressed;
            }
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }
    }

    private static final class MappingDialog extends Dialog {
        private MappingView mMappingView;

        MappingDialog(@NonNull Activity activity) {
            super(activity);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            requestWindowFeature(Window.FEATURE_NO_TITLE);
            mMappingView = new MappingView(getContext(), this);
            setContentView(mMappingView);
            setCanceledOnTouchOutside(false);

            final Window window = getWindow();
            if (window != null) {
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                window.getDecorView().setSystemUiVisibility(
                        View.SYSTEM_UI_FLAG_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
                        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION |
                        View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
            }
        }

        @Override
        protected void onStart() {
            super.onStart();
            final Window window = getWindow();
            if (window != null) {
                window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT);
            }
            if (mMappingView != null) {
                mMappingView.requestFocus();
            }
        }

        @Override
        public boolean dispatchKeyEvent(@NonNull KeyEvent event) {
            return mMappingView != null && mMappingView.handleKeyEvent(event) ||
                    super.dispatchKeyEvent(event);
        }

        @Override
        public boolean dispatchGenericMotionEvent(@NonNull MotionEvent event) {
            return mMappingView != null && mMappingView.handleMotionEvent(event) ||
                    super.dispatchGenericMotionEvent(event);
        }
    }

    private static final class MappingView extends View {
        private static final float DESIGN_WIDTH = 1600.0f;
        private static final float DESIGN_HEIGHT = 900.0f;
        private static final int VISIBLE_ROWS = 9;
        private static final float ROW_TOP = 145.0f;
        private static final float ROW_HEIGHT = 62.0f;
        private static final float ROW_GAP = 7.0f;
        private static final float AXIS_CAPTURE_THRESHOLD = 0.65f;
        private static final float AXIS_NEUTRAL_THRESHOLD = 0.25f;
        private static final long NAV_REPEAT_MS = 190L;
        private static final RectF RESET_RECT = new RectF(170, 790, 485, 846);
        private static final RectF CLEAR_RECT = new RectF(515, 790, 830, 846);
        private static final RectF DONE_RECT = new RectF(1115, 790, 1430, 846);

        private static final String[] CONTROL_NAMES = {
                "Analog Up", "Analog Down", "Analog Left", "Analog Right",
                "A Button", "B Button", "Z Trigger", "Start",
                "L Shoulder", "R Shoulder",
                "C Up", "C Down", "C Left", "C Right",
                "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
                "Emulator Menu"
        };

        private static final int[] CONTROL_COMMANDS = {
                InputMap.AXIS_U, InputMap.AXIS_D, InputMap.AXIS_L, InputMap.AXIS_R,
                AbstractController.BTN_A, AbstractController.BTN_B,
                AbstractController.BTN_Z, AbstractController.START,
                AbstractController.BTN_L, AbstractController.BTN_R,
                AbstractController.CPD_U, AbstractController.CPD_D,
                AbstractController.CPD_L, AbstractController.CPD_R,
                AbstractController.DPD_U, AbstractController.DPD_D,
                AbstractController.DPD_L, AbstractController.DPD_R,
                InputMap.FUNC_SIMULATE_BACK
        };

        private static final int[] CAPTURE_AXES = {
                MotionEvent.AXIS_X, MotionEvent.AXIS_Y,
                MotionEvent.AXIS_Z, MotionEvent.AXIS_RZ,
                MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_HAT_Y,
                MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_RTRIGGER,
                MotionEvent.AXIS_BRAKE, MotionEvent.AXIS_GAS
        };

        private final Dialog mDialog;
        private final AppData mAppData;
        private final GlobalPrefs mGlobalPrefs;
        private InputMap mMap;
        private String mProfileName;
        private String mProfileComment;
        private boolean mAutoDeadzone;
        private int mDeadzone;
        private int mSensitivityX;
        private int mSensitivityY;
        private int mSelected;
        private int mFirstVisible;
        private boolean mCapturing;
        private boolean mCaptureAxisReady;
        private boolean mTriggerReady;
        private boolean mTriggerPressed;
        private long mNextNavigationTime;
        private long mIgnoreBackUntil;
        private float mTouchDownY;
        private String mStatus = "Select a row, then press a Quest button or move an axis";

        MappingView(@NonNull Context context, @NonNull Dialog dialog) {
            super(context);
            mDialog = dialog;
            mAppData = new AppData(context);
            mGlobalPrefs = new GlobalPrefs(context, mAppData);
            loadActiveProfile();
            setFocusable(true);
            setFocusableInTouchMode(true);
            setClickable(true);
        }

        private void loadActiveProfile() {
            final SharedPreferences preferences =
                    PreferenceManager.getDefaultSharedPreferences(getContext());
            String requestedName = preferences.getString(
                    GlobalPrefs.CONTROLLER_PROFILE1, "Android Gamepad");
            final ConfigFile custom = new ConfigFile(mGlobalPrefs.controllerProfiles_cfg);
            final ConfigFile builtin = mAppData.GetControllerProfilesConfig();

            ConfigSection section = custom.get(requestedName);
            boolean isBuiltin = false;
            if (section == null) {
                section = builtin.get(requestedName);
                isBuiltin = section != null;
            }
            if (section == null) {
                requestedName = "Android Gamepad";
                section = custom.get(requestedName);
            }
            if (section == null) {
                section = builtin.get(requestedName);
                isBuiltin = section != null;
            }

            if (section != null) {
                final ControllerProfile profile = new ControllerProfile(isBuiltin, section);
                mProfileName = profile.getName();
                mProfileComment = profile.getComment();
                mMap = profile.getMap();
                mAutoDeadzone = profile.getAutoDeadzone();
                mDeadzone = profile.getDeadzone();
                mSensitivityX = profile.getSensitivityX();
                mSensitivityY = profile.getSensitivityY();
            } else {
                mProfileName = "KQ64 Quest Controller";
                mProfileComment = "Created by the KQ64 launcher";
                mMap = new InputMap();
                mAutoDeadzone = true;
                mDeadzone = 15;
                mSensitivityX = 100;
                mSensitivityY = 100;
            }
        }

        private void persistMap() {
            final File profilesDirectory = new File(mGlobalPrefs.profilesDir);
            if (!profilesDirectory.exists() && !profilesDirectory.mkdirs()) {
                mStatus = "Could not create the controller profile folder";
                QuestVrDiagnostics.warn(TAG, mStatus);
                invalidate();
                return;
            }

            final ConfigFile custom = new ConfigFile(mGlobalPrefs.controllerProfiles_cfg);
            custom.put(mProfileName, "comment", TextUtils.isEmpty(mProfileComment) ?
                    "Edited in the KQ64 launcher" : mProfileComment);
            custom.put(mProfileName, "map", mMap.serialize());
            custom.put(mProfileName, "auto_deadzone", Boolean.toString(mAutoDeadzone));
            custom.put(mProfileName, "deadzone", Integer.toString(mDeadzone));
            custom.put(mProfileName, "sensitivity_x", Integer.toString(mSensitivityX));
            custom.put(mProfileName, "sensitivity_y", Integer.toString(mSensitivityY));
            final boolean saved = custom.save();
            final boolean selected = PreferenceManager.getDefaultSharedPreferences(getContext())
                    .edit().putString(GlobalPrefs.CONTROLLER_PROFILE1, mProfileName).commit();
            mStatus = saved && selected ? "Saved to Player 1 profile: " + mProfileName :
                    "Could not save the controller profile";
            QuestVrDiagnostics.info(TAG, mStatus);
            invalidate();
        }

        private void resetToDefault() {
            final ConfigSection section =
                    mAppData.GetControllerProfilesConfig().get("Android Gamepad");
            if (section == null) {
                mStatus = "Built-in Android Gamepad profile is unavailable";
                invalidate();
                return;
            }
            final ControllerProfile profile = new ControllerProfile(true, section);
            mProfileName = profile.getName();
            mProfileComment = profile.getComment();
            mMap = profile.getMap();
            mAutoDeadzone = profile.getAutoDeadzone();
            mDeadzone = profile.getDeadzone();
            mSensitivityX = profile.getSensitivityX();
            mSensitivityY = profile.getSensitivityY();
            mCapturing = false;
            persistMap();
            mStatus = "Restored Android Gamepad defaults";
            invalidate();
        }

        private void beginCapture() {
            mCapturing = true;
            mCaptureAxisReady = false;
            mStatus = "Listening for " + CONTROL_NAMES[mSelected] +
                    " — press a button or move an axis";
            invalidate();
        }

        private void cancelCapture() {
            mCapturing = false;
            mStatus = "Mapping cancelled";
            invalidate();
        }

        private void clearSelected() {
            mMap.unmapCommand(CONTROL_COMMANDS[mSelected]);
            mCapturing = false;
            persistMap();
            mStatus = "Cleared " + CONTROL_NAMES[mSelected];
            invalidate();
        }

        private void bindInput(int inputCode) {
            if (inputCode == 0) {
                return;
            }
            final int command = CONTROL_COMMANDS[mSelected];
            mMap.unmapCommand(command);
            mMap.map(inputCode, command);
            mCapturing = false;
            persistMap();
            mStatus = CONTROL_NAMES[mSelected] + " = " + friendlyInputName(inputCode);
            invalidate();
        }

        boolean handleKeyEvent(@NonNull KeyEvent event) {
            final int keyCode = event.getKeyCode();
            final boolean gamepad = (event.getSource() &
                    (InputDevice.SOURCE_GAMEPAD | InputDevice.SOURCE_JOYSTICK)) != 0;
            if (event.getAction() == KeyEvent.ACTION_UP) {
                return gamepad || keyCode == KeyEvent.KEYCODE_BACK ||
                        keyCode == KeyEvent.KEYCODE_ESCAPE;
            }
            if (event.getAction() != KeyEvent.ACTION_DOWN || event.getRepeatCount() != 0) {
                return false;
            }

            if (mCapturing) {
                if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_ESCAPE) {
                    cancelCapture();
                } else {
                    bindInput(keyCode);
                    if (keyCode == KeyEvent.KEYCODE_BUTTON_B) {
                        mIgnoreBackUntil = SystemClock.uptimeMillis() + 600L;
                    }
                }
                return true;
            }

            switch (keyCode) {
                case KeyEvent.KEYCODE_DPAD_UP:
                    moveSelection(-1);
                    return true;
                case KeyEvent.KEYCODE_DPAD_DOWN:
                    moveSelection(1);
                    return true;
                case KeyEvent.KEYCODE_DPAD_LEFT:
                case KeyEvent.KEYCODE_BUTTON_L1:
                    moveSelection(-VISIBLE_ROWS);
                    return true;
                case KeyEvent.KEYCODE_DPAD_RIGHT:
                case KeyEvent.KEYCODE_BUTTON_R1:
                    moveSelection(VISIBLE_ROWS);
                    return true;
                case KeyEvent.KEYCODE_BUTTON_A:
                case KeyEvent.KEYCODE_ENTER:
                case KeyEvent.KEYCODE_DPAD_CENTER:
                    beginCapture();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_X:
                    clearSelected();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_Y:
                    resetToDefault();
                    return true;
                case KeyEvent.KEYCODE_BUTTON_B:
                    mDialog.dismiss();
                    return true;
                case KeyEvent.KEYCODE_BACK:
                    if (SystemClock.uptimeMillis() < mIgnoreBackUntil) {
                        return true;
                    }
                    mDialog.dismiss();
                    return true;
                case KeyEvent.KEYCODE_ESCAPE:
                    mDialog.dismiss();
                    return true;
                default:
                    return gamepad;
            }
        }

        boolean handleMotionEvent(@NonNull MotionEvent event) {
            if (event.getActionMasked() != MotionEvent.ACTION_MOVE ||
                    (event.getSource() & (InputDevice.SOURCE_JOYSTICK |
                            InputDevice.SOURCE_GAMEPAD)) == 0) {
                return false;
            }

            if (mCapturing) {
                float strongest = 0.0f;
                int strongestAxis = -1;
                for (int axis : CAPTURE_AXES) {
                    final float value = event.getAxisValue(axis);
                    if (Math.abs(value) > Math.abs(strongest)) {
                        strongest = value;
                        strongestAxis = axis;
                    }
                }

                if (!mCaptureAxisReady) {
                    if (Math.abs(strongest) <= AXIS_NEUTRAL_THRESHOLD) {
                        mCaptureAxisReady = true;
                    }
                    return true;
                }

                if (strongestAxis >= 0 && Math.abs(strongest) >= AXIS_CAPTURE_THRESHOLD) {
                    bindInput(axisToInputCode(strongestAxis, strongest > 0.0f));
                }
                return true;
            }

            final float horizontal = strongestAxis(event,
                    MotionEvent.AXIS_HAT_X, MotionEvent.AXIS_X);
            final float vertical = strongestAxis(event,
                    MotionEvent.AXIS_HAT_Y, MotionEvent.AXIS_Y);
            final float trigger = Math.max(
                    Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER),
                            event.getAxisValue(MotionEvent.AXIS_RTRIGGER)),
                    Math.max(event.getAxisValue(MotionEvent.AXIS_BRAKE),
                            event.getAxisValue(MotionEvent.AXIS_GAS)));

            if (!mTriggerReady && trigger <= AXIS_NEUTRAL_THRESHOLD) {
                mTriggerReady = true;
            }
            if (mTriggerReady && trigger >= 0.72f && !mTriggerPressed) {
                beginCapture();
            }
            mTriggerPressed = trigger >= 0.72f;

            final long now = SystemClock.uptimeMillis();
            if (now >= mNextNavigationTime &&
                    Math.abs(vertical) >= 0.62f &&
                    Math.abs(vertical) >= Math.abs(horizontal)) {
                moveSelection(vertical < 0.0f ? -1 : 1);
                mNextNavigationTime = now + NAV_REPEAT_MS;
            } else if (Math.abs(vertical) < AXIS_NEUTRAL_THRESHOLD) {
                mNextNavigationTime = 0L;
            }
            return true;
        }

        private void moveSelection(int amount) {
            mSelected = Math.max(0, Math.min(CONTROL_NAMES.length - 1,
                    mSelected + amount));
            ensureSelectedVisible();
            mStatus = "A / Trigger maps • X clears • Y resets • B closes";
            invalidate();
        }

        private void ensureSelectedVisible() {
            if (mSelected < mFirstVisible) {
                mFirstVisible = mSelected;
            } else if (mSelected >= mFirstVisible + VISIBLE_ROWS) {
                mFirstVisible = mSelected - VISIBLE_ROWS + 1;
            }
            final int maximum = Math.max(0, CONTROL_NAMES.length - VISIBLE_ROWS);
            mFirstVisible = Math.max(0, Math.min(maximum, mFirstVisible));
        }

        @Override
        protected void onDraw(@NonNull Canvas canvas) {
            super.onDraw(canvas);
            final float sx = getWidth() / DESIGN_WIDTH;
            final float sy = getHeight() / DESIGN_HEIGHT;
            canvas.save();
            canvas.scale(sx, sy);

            final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
            paint.setShader(new LinearGradient(0, 0, 1600, 900,
                    Color.rgb(2, 6, 17), Color.rgb(5, 24, 48), Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, 1600, 900, paint);
            paint.setShader(new RadialGradient(800, 420, 760,
                    new int[]{Color.argb(100, 0, 150, 255), Color.argb(20, 0, 90, 180),
                            Color.TRANSPARENT}, null, Shader.TileMode.CLAMP));
            canvas.drawCircle(800, 420, 760, paint);
            paint.setShader(null);

            drawText(canvas, paint, "KQ64 VR", 72, 72, 42,
                    Color.WHITE, Paint.Align.LEFT, true);
            drawText(canvas, paint, "CONTROLLER MAPPING", 75, 108, 18,
                    Color.rgb(54, 202, 255), Paint.Align.LEFT, true);
            drawText(canvas, paint, "PLAYER 1  •  " + mProfileName, 1525, 87, 20,
                    Color.rgb(167, 207, 229), Paint.Align.RIGHT, false);

            for (int visible = 0; visible < VISIBLE_ROWS; ++visible) {
                final int index = mFirstVisible + visible;
                if (index >= CONTROL_NAMES.length) {
                    break;
                }
                final float top = ROW_TOP + visible * (ROW_HEIGHT + ROW_GAP);
                final RectF row = new RectF(170, top, 1430, top + ROW_HEIGHT);
                final boolean selected = index == mSelected;
                final boolean listening = selected && mCapturing;
                drawRow(canvas, paint, row, CONTROL_NAMES[index],
                        listening ? "PRESS A BUTTON OR MOVE AN AXIS…" :
                                mappedName(CONTROL_COMMANDS[index]),
                        selected, listening);
            }

            if (mFirstVisible > 0) {
                drawText(canvas, paint, "▲", 800, 137, 18,
                        Color.rgb(54, 202, 255), Paint.Align.CENTER, true);
            }
            if (mFirstVisible + VISIBLE_ROWS < CONTROL_NAMES.length) {
                drawText(canvas, paint, "▼", 800, 779, 18,
                        Color.rgb(54, 202, 255), Paint.Align.CENTER, true);
            }

            drawFooterButton(canvas, paint, RESET_RECT, "RESET DEFAULT", false);
            drawFooterButton(canvas, paint, CLEAR_RECT, "CLEAR SELECTED", false);
            drawFooterButton(canvas, paint, DONE_RECT, "DONE", true);
            drawText(canvas, paint, mStatus, 800, 878, 17,
                    Color.rgb(125, 166, 190), Paint.Align.CENTER, false);
            canvas.restore();
        }

        private void drawRow(Canvas canvas, Paint paint, RectF row, String label, String value,
                boolean selected, boolean listening) {
            paint.setStyle(Paint.Style.FILL);
            paint.setShader(new LinearGradient(row.left, row.top, row.right, row.bottom,
                    selected ? Color.argb(240, 8, 55, 93) : Color.argb(220, 5, 25, 47),
                    Color.argb(235, 3, 13, 31), Shader.TileMode.CLAMP));
            canvas.drawRoundRect(row, 17, 17, paint);
            paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(selected ? 4 : 2);
            paint.setColor(listening ? Color.rgb(255, 196, 66) : selected ?
                    Color.rgb(53, 211, 255) : Color.argb(85, 52, 126, 168));
            canvas.drawRoundRect(row, 17, 17, paint);
            paint.setStyle(Paint.Style.FILL);
            drawText(canvas, paint, label, row.left + 28, row.centerY() + 8, 21,
                    Color.WHITE, Paint.Align.LEFT, true);
            drawText(canvas, paint, value, row.right - 28, row.centerY() + 8, 18,
                    listening ? Color.rgb(255, 210, 90) : Color.rgb(145, 190, 216),
                    Paint.Align.RIGHT, false);
        }

        private void drawFooterButton(Canvas canvas, Paint paint, RectF rect, String label,
                boolean primary) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(primary ? Color.rgb(9, 89, 139) : Color.argb(230, 5, 31, 56));
            canvas.drawRoundRect(rect, 17, 17, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(primary ? 3 : 2);
            paint.setColor(primary ? Color.rgb(53, 211, 255) :
                    Color.argb(120, 52, 126, 168));
            canvas.drawRoundRect(rect, 17, 17, paint);
            paint.setStyle(Paint.Style.FILL);
            drawText(canvas, paint, label, rect.centerX(), rect.centerY() + 7, 17,
                    Color.WHITE, Paint.Align.CENTER, true);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event) {
            final float x = event.getX() * DESIGN_WIDTH / Math.max(1, getWidth());
            final float y = event.getY() * DESIGN_HEIGHT / Math.max(1, getHeight());
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                mTouchDownY = y;
                return true;
            }
            if (event.getActionMasked() != MotionEvent.ACTION_UP) {
                return true;
            }

            if (Math.abs(y - mTouchDownY) > 45.0f) {
                moveSelection(y < mTouchDownY ? VISIBLE_ROWS : -VISIBLE_ROWS);
                return true;
            }

            if (RESET_RECT.contains(x, y)) {
                resetToDefault();
                return true;
            }
            if (CLEAR_RECT.contains(x, y)) {
                clearSelected();
                return true;
            }
            if (DONE_RECT.contains(x, y)) {
                mDialog.dismiss();
                return true;
            }

            if (x >= 145 && x <= 1455 && y >= ROW_TOP && y <= 775) {
                final int visible = (int) ((y - ROW_TOP) / (ROW_HEIGHT + ROW_GAP));
                final float within = (y - ROW_TOP) - visible * (ROW_HEIGHT + ROW_GAP);
                final int index = mFirstVisible + visible;
                if (within <= ROW_HEIGHT && index >= 0 && index < CONTROL_NAMES.length) {
                    mSelected = index;
                    beginCapture();
                }
            }
            return true;
        }

        @Override
        public boolean performClick() {
            super.performClick();
            return true;
        }

        private String mappedName(int command) {
            final String mapped = mMap.getMappedCodeInfo(command);
            if (TextUtils.isEmpty(mapped)) {
                return "UNMAPPED";
            }
            return mapped.replace("\n", " + ");
        }

        private static String friendlyInputName(int inputCode) {
            String name = AbstractProvider.getInputName(inputCode);
            name = name.replace("KEYCODE_BUTTON_", "")
                    .replace("KEYCODE_DPAD_", "D-PAD ")
                    .replace("KEYCODE_", "")
                    .replace("AXIS_", "")
                    .replace('_', ' ');
            return name;
        }

        private static int axisToInputCode(int axisCode, boolean positiveDirection) {
            return -((axisCode) * 2 + (positiveDirection ? 1 : 2));
        }

        private static float strongestAxis(MotionEvent event, int firstAxis, int secondAxis) {
            final float first = event.getAxisValue(firstAxis);
            final float second = event.getAxisValue(secondAxis);
            return Math.abs(first) >= Math.abs(second) ? first : second;
        }

        private static void drawText(Canvas canvas, Paint paint, String text, float x, float y,
                float size, int color, Paint.Align align, boolean bold) {
            paint.setShader(null);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(color);
            paint.setTextSize(size);
            paint.setTextAlign(align);
            paint.setTypeface(bold ? android.graphics.Typeface.DEFAULT_BOLD :
                    android.graphics.Typeface.DEFAULT);
            canvas.drawText(text == null ? "" : text, x, y, paint);
        }
    }
}
