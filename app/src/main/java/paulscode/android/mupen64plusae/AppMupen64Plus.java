/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * Copyright (C) 2013 Paul Lamb
 *
 * This file is part of Mupen64PlusAE.
 *
 * Mupen64PlusAE is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 */
package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.app.Application;
import android.app.Dialog;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
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
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;

import androidx.activity.OnBackPressedCallback;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.multidex.MultiDexApplication;
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

public class AppMupen64Plus extends MultiDexApplication
{
    private static final String EXTRA_KQ64_ADVANCED =
            "paulscode.android.mupen64plusae.extra.KQ64_ADVANCED";
    private static final String CONTROLS_OVERLAY_TAG = "kq64-controls-overlay";

    @Override
    public void onCreate()
    {
        super.onCreate();
        registerActivityLifecycleCallbacks(new Kq64LauncherIntegration());
    }

    /**
     * Keeps the Advanced/Legacy route from resuming the previous ROM and adds the lightweight
     * controller-mapping entry point to the Quest launcher without changing GameActivity or OpenXR.
     */
    private static final class Kq64LauncherIntegration
            implements Application.ActivityLifecycleCallbacks
    {
        private boolean mLauncherWasLastActivity;

        @Override
        public void onActivityPreCreated(Activity activity, Bundle savedInstanceState)
        {
            if (activity instanceof GalleryActivity && mLauncherWasLastActivity)
            {
                final Intent intent = activity.getIntent();
                if (intent != null && intent.getExtras() == null)
                {
                    intent.putExtra(EXTRA_KQ64_ADVANCED, true);
                }
                mLauncherWasLastActivity = false;
            }
            else if (!(activity instanceof QuestVrLauncherActivity))
            {
                mLauncherWasLastActivity = false;
            }
        }

        @Override
        public void onActivityCreated(Activity activity, Bundle savedInstanceState)
        {
            if (activity instanceof QuestVrLauncherActivity)
            {
                installControlsEntry((AppCompatActivity) activity);
                return;
            }

            final Intent intent = activity.getIntent();
            if (!(activity instanceof GalleryActivity) || intent == null ||
                    !intent.getBooleanExtra(EXTRA_KQ64_ADVANCED, false))
            {
                return;
            }

            final AppCompatActivity gallery = (AppCompatActivity) activity;
            gallery.getOnBackPressedDispatcher().addCallback(gallery,
                    new OnBackPressedCallback(true)
                    {
                        @Override
                        public void handleOnBackPressed()
                        {
                            setEnabled(false);
                            final Intent launcher =
                                    new Intent(gallery, QuestVrLauncherActivity.class);
                            launcher.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP |
                                    Intent.FLAG_ACTIVITY_SINGLE_TOP);
                            gallery.startActivity(launcher);
                            gallery.finish();
                            gallery.overridePendingTransition(0, 0);
                        }
                    });
        }

        private static void installControlsEntry(AppCompatActivity activity)
        {
            final View contentView = activity.findViewById(android.R.id.content);
            if (!(contentView instanceof ViewGroup))
            {
                QuestVrDiagnostics.warn("Kq64Controls",
                        "Launcher content root unavailable; controller mapping button not installed");
                return;
            }

            final ViewGroup content = (ViewGroup) contentView;
            if (content.findViewWithTag(CONTROLS_OVERLAY_TAG) != null)
            {
                return;
            }

            final Kq64ControlsEntryView controls = new Kq64ControlsEntryView(activity);
            controls.setTag(CONTROLS_OVERLAY_TAG);
            content.addView(controls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            QuestVrDiagnostics.info("Kq64Controls",
                    "Installed launcher Controls button and Player 1 mapping panel");
        }

        @Override
        public void onActivityResumed(Activity activity)
        {
            mLauncherWasLastActivity = activity instanceof QuestVrLauncherActivity;
        }

        @Override public void onActivityStarted(Activity activity) {}
        @Override public void onActivityPaused(Activity activity) {}
        @Override public void onActivityStopped(Activity activity) {}
        @Override public void onActivitySaveInstanceState(Activity activity, Bundle outState) {}
        @Override public void onActivityDestroyed(Activity activity) {}
    }

    /**
     * A small, input-transparent overlay except for the unused right-hand slot beside Advanced.
     */
    private static final class Kq64ControlsEntryView extends View
    {
        private static final float DESIGN_WIDTH = 1600.0f;
        private static final float DESIGN_HEIGHT = 900.0f;
        private static final RectF BUTTON_RECT = new RectF(1310, 710, 1525, 790);

        private final AppCompatActivity mActivity;
        private boolean mPressed;

        Kq64ControlsEntryView(AppCompatActivity activity)
        {
            super(activity);
            mActivity = activity;
            setWillNotDraw(false);
            setClickable(true);
            setFocusable(false);
        }

        @Override
        protected void onDraw(@NonNull Canvas canvas)
        {
            super.onDraw(canvas);
            final float sx = getWidth() / DESIGN_WIDTH;
            final float sy = getHeight() / DESIGN_HEIGHT;
            canvas.save();
            canvas.scale(sx, sy);

            final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
            final RectF rect = BUTTON_RECT;
            paint.setStyle(Paint.Style.FILL);
            paint.setShader(new LinearGradient(rect.left, rect.top, rect.right, rect.bottom,
                    mPressed ? Color.argb(245, 10, 67, 108) : Color.argb(235, 8, 48, 83),
                    Color.argb(235, 3, 13, 31), Shader.TileMode.CLAMP));
            canvas.drawRoundRect(rect, 25, 25, paint);
            paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(mPressed ? 5 : 3);
            paint.setColor(mPressed ? Color.rgb(53, 211, 255) :
                    Color.argb(190, 19, 158, 222));
            canvas.drawRoundRect(rect, 25, 25, paint);
            paint.setStyle(Paint.Style.FILL);
            drawText(canvas, paint, "CONTROLS", rect.centerX(), rect.centerY() + 7,
                    18, Color.WHITE, Paint.Align.CENTER, true);
            canvas.restore();
        }

        @Override
        public boolean onTouchEvent(MotionEvent event)
        {
            final float x = event.getX() * DESIGN_WIDTH / Math.max(1, getWidth());
            final float y = event.getY() * DESIGN_HEIGHT / Math.max(1, getHeight());
            final boolean inside = BUTTON_RECT.contains(x, y);
            switch (event.getActionMasked())
            {
                case MotionEvent.ACTION_DOWN:
                    if (!inside)
                    {
                        return false;
                    }
                    mPressed = true;
                    invalidate();
                    return true;
                case MotionEvent.ACTION_MOVE:
                    if (!mPressed)
                    {
                        return false;
                    }
                    mPressed = inside;
                    invalidate();
                    return true;
                case MotionEvent.ACTION_UP:
                    if (!mPressed)
                    {
                        return false;
                    }
                    mPressed = false;
                    invalidate();
                    if (inside)
                    {
                        performClick();
                        new Kq64ControllerDialog(mActivity).show();
                    }
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    if (mPressed)
                    {
                        mPressed = false;
                        invalidate();
                        return true;
                    }
                    return false;
                default:
                    return mPressed;
            }
        }

        @Override
        public boolean performClick()
        {
            super.performClick();
            return true;
        }
    }

    private static final class Kq64ControllerDialog extends Dialog
    {
        private Kq64ControllerMapView mMapView;

        Kq64ControllerDialog(@NonNull Activity activity)
        {
            super(activity);
        }

        @Override
        protected void onCreate(Bundle savedInstanceState)
        {
            super.onCreate(savedInstanceState);
            requestWindowFeature(Window.FEATURE_NO_TITLE);
            mMapView = new Kq64ControllerMapView(getContext(), this);
            setContentView(mMapView);
            setCanceledOnTouchOutside(false);

            final Window window = getWindow();
            if (window != null)
            {
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
        protected void onStart()
        {
            super.onStart();
            final Window window = getWindow();
            if (window != null)
            {
                window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.MATCH_PARENT);
            }
            if (mMapView != null)
            {
                mMapView.requestFocus();
            }
        }

        @Override
        public boolean dispatchKeyEvent(@NonNull KeyEvent event)
        {
            return mMapView != null && mMapView.handleKeyEvent(event) ||
                    super.dispatchKeyEvent(event);
        }

        @Override
        public boolean dispatchGenericMotionEvent(@NonNull MotionEvent event)
        {
            return mMapView != null && mMapView.handleMotionEvent(event) ||
                    super.dispatchGenericMotionEvent(event);
        }
    }

    private static final class Kq64ControllerMapView extends View
    {
        private static final String TAG = "Kq64Controls";
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
        private float mTouchDownY;
        private String mStatus = "Select a row, then press a Quest button or move an axis";

        Kq64ControllerMapView(Context context, Dialog dialog)
        {
            super(context);
            mDialog = dialog;
            mAppData = new AppData(context);
            mGlobalPrefs = new GlobalPrefs(context, mAppData);
            loadActiveProfile();
            setFocusable(true);
            setFocusableInTouchMode(true);
            setClickable(true);
        }

        private void loadActiveProfile()
        {
            final SharedPreferences preferences =
                    PreferenceManager.getDefaultSharedPreferences(getContext());
            String requestedName = preferences.getString(
                    GlobalPrefs.CONTROLLER_PROFILE1, "Android Gamepad");
            final ConfigFile custom = mGlobalPrefs.GetControllerProfilesConfig();
            final ConfigFile builtin = mAppData.GetControllerProfilesConfig();
            ConfigSection section = custom.get(requestedName);
            boolean isBuiltin = false;

            if (section == null)
            {
                section = builtin.get(requestedName);
                isBuiltin = section != null;
            }
            if (section == null)
            {
                requestedName = "Android Gamepad";
                section = custom.get(requestedName);
            }
            if (section == null)
            {
                section = builtin.get(requestedName);
                isBuiltin = section != null;
            }

            if (section != null)
            {
                final ControllerProfile profile = new ControllerProfile(isBuiltin, section);
                mProfileName = profile.getName();
                mProfileComment = profile.getComment();
                mMap = profile.getMap();
                mAutoDeadzone = profile.getAutoDeadzone();
                mDeadzone = profile.getDeadzone();
                mSensitivityX = profile.getSensitivityX();
                mSensitivityY = profile.getSensitivityY();
            }
            else
            {
                mProfileName = "KQ64 Quest Controller";
                mProfileComment = "Created by the KQ64 launcher";
                mMap = new InputMap();
                mAutoDeadzone = true;
                mDeadzone = 15;
                mSensitivityX = 100;
                mSensitivityY = 100;
            }
        }

        private void persistMap()
        {
            final File profilesDirectory = new File(mGlobalPrefs.profilesDir);
            if (!profilesDirectory.exists() && !profilesDirectory.mkdirs())
            {
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
            PreferenceManager.getDefaultSharedPreferences(getContext()).edit()
                    .putString(GlobalPrefs.CONTROLLER_PROFILE1, mProfileName)
                    .commit();
            mStatus = saved ? "Saved to Player 1 profile: " + mProfileName :
                    "Could not save the controller profile";
            if (saved)
            {
                QuestVrDiagnostics.info(TAG,
                        "Saved Player 1 controller map profile=" + mProfileName);
            }
            else
            {
                QuestVrDiagnostics.warn(TAG, mStatus);
            }
            invalidate();
        }

        private void resetToDefault()
        {
            final ConfigSection section =
                    mAppData.GetControllerProfilesConfig().get("Android Gamepad");
            if (section == null)
            {
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
            mStatus = "Restored the Android Gamepad defaults";
            invalidate();
        }

        private void beginCapture()
        {
            mCapturing = true;
            mCaptureAxisReady = false;
            mStatus = "Listening for " + CONTROL_NAMES[mSelected] +
                    " — press a button or move an axis";
            invalidate();
        }

        private void cancelCapture()
        {
            mCapturing = false;
            mStatus = "Mapping cancelled";
            invalidate();
        }

        private void clearSelected()
        {
            mMap.unmapCommand(CONTROL_COMMANDS[mSelected]);
            mCapturing = false;
            persistMap();
            mStatus = "Cleared " + CONTROL_NAMES[mSelected];
            invalidate();
        }

        private void bindInput(int inputCode)
        {
            if (inputCode == 0)
            {
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

        boolean handleKeyEvent(KeyEvent event)
        {
            final int keyCode = event.getKeyCode();
            if (event.getAction() == KeyEvent.ACTION_UP)
            {
                return isControllerKey(keyCode);
            }
            if (event.getAction() != KeyEvent.ACTION_DOWN || event.getRepeatCount() != 0)
            {
                return false;
            }

            if (mCapturing)
            {
                if (keyCode == KeyEvent.KEYCODE_BACK || keyCode == KeyEvent.KEYCODE_ESCAPE)
                {
                    cancelCapture();
                }
                else
                {
                    bindInput(keyCode);
                }
                return true;
            }

            switch (keyCode)
            {
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
                case KeyEvent.KEYCODE_BACK:
                case KeyEvent.KEYCODE_ESCAPE:
                    mDialog.dismiss();
                    return true;
                default:
                    return false;
            }
        }

        boolean handleMotionEvent(MotionEvent event)
        {
            if (event.getActionMasked() != MotionEvent.ACTION_MOVE ||
                    (event.getSource() & (InputDevice.SOURCE_JOYSTICK |
                            InputDevice.SOURCE_GAMEPAD)) == 0)
            {
                return false;
            }

            if (mCapturing)
            {
                float strongest = 0.0f;
                int strongestAxis = -1;
                for (int axis : CAPTURE_AXES)
                {
                    final float value = event.getAxisValue(axis);
                    if (Math.abs(value) > Math.abs(strongest))
                    {
                        strongest = value;
                        strongestAxis = axis;
                    }
                }

                if (!mCaptureAxisReady)
                {
                    if (Math.abs(strongest) <= AXIS_NEUTRAL_THRESHOLD)
                    {
                        mCaptureAxisReady = true;
                    }
                    return true;
                }

                if (strongestAxis >= 0 && Math.abs(strongest) >= AXIS_CAPTURE_THRESHOLD)
                {
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

            if (!mTriggerReady && trigger <= AXIS_NEUTRAL_THRESHOLD)
            {
                mTriggerReady = true;
            }
            if (mTriggerReady && trigger >= 0.72f && !mTriggerPressed)
            {
                beginCapture();
            }
            mTriggerPressed = trigger >= 0.72f;

            final long now = SystemClock.uptimeMillis();
            if (now >= mNextNavigationTime &&
                    Math.abs(vertical) >= 0.62f &&
                    Math.abs(vertical) >= Math.abs(horizontal))
            {
                moveSelection(vertical < 0.0f ? -1 : 1);
                mNextNavigationTime = now + NAV_REPEAT_MS;
            }
            else if (Math.abs(vertical) < AXIS_NEUTRAL_THRESHOLD)
            {
                mNextNavigationTime = 0L;
            }
            return true;
        }

        private void moveSelection(int amount)
        {
            mSelected = Math.max(0, Math.min(CONTROL_NAMES.length - 1,
                    mSelected + amount));
            ensureSelectedVisible();
            mStatus = "A / Trigger maps • X clears • Y resets • B closes";
            invalidate();
        }

        private void ensureSelectedVisible()
        {
            if (mSelected < mFirstVisible)
            {
                mFirstVisible = mSelected;
            }
            else if (mSelected >= mFirstVisible + VISIBLE_ROWS)
            {
                mFirstVisible = mSelected - VISIBLE_ROWS + 1;
            }
            final int maximum = Math.max(0, CONTROL_NAMES.length - VISIBLE_ROWS);
            mFirstVisible = Math.max(0, Math.min(maximum, mFirstVisible));
        }

        @Override
        protected void onDraw(@NonNull Canvas canvas)
        {
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

            for (int visible = 0; visible < VISIBLE_ROWS; ++visible)
            {
                final int index = mFirstVisible + visible;
                if (index >= CONTROL_NAMES.length)
                {
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

            if (mFirstVisible > 0)
            {
                drawText(canvas, paint, "▲", 800, 137, 18,
                        Color.rgb(54, 202, 255), Paint.Align.CENTER, true);
            }
            if (mFirstVisible + VISIBLE_ROWS < CONTROL_NAMES.length)
            {
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
                boolean selected, boolean listening)
        {
            paint.setStyle(Paint.Style.FILL);
            paint.setShader(new LinearGradient(row.left, row.top, row.right, row.bottom,
                    selected ? Color.argb(240, 8, 55, 93) : Color.argb(220, 5, 25, 47),
                    Color.argb(235, 3, 13, 31), Shader.TileMode.CLAMP));
            canvas.drawRoundRect(row, 17, 17, paint);
            paint.setShader(null);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(selected ? 4 : 2);
            paint.setColor(listening ? Color.rgb(255, 189, 71) :
                    selected ? Color.rgb(53, 211, 255) :
                            Color.argb(85, 52, 126, 168));
            canvas.drawRoundRect(row, 17, 17, paint);
            paint.setStyle(Paint.Style.FILL);
            drawText(canvas, paint, label, row.left + 26, row.centerY() + 7, 20,
                    Color.WHITE, Paint.Align.LEFT, true);
            drawText(canvas, paint, value, row.right - 26, row.centerY() + 7, 18,
                    listening ? Color.rgb(255, 211, 126) :
                            TextUtils.equals(value, "NOT MAPPED") ?
                                    Color.rgb(102, 133, 151) : Color.rgb(145, 211, 239),
                    Paint.Align.RIGHT, false);
        }

        private void drawFooterButton(Canvas canvas, Paint paint, RectF rect, String text,
                boolean primary)
        {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(primary ? Color.argb(235, 8, 69, 112) :
                    Color.argb(220, 5, 25, 47));
            canvas.drawRoundRect(rect, 18, 18, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(primary ? 3 : 2);
            paint.setColor(primary ? Color.rgb(53, 211, 255) :
                    Color.argb(140, 52, 126, 168));
            canvas.drawRoundRect(rect, 18, 18, paint);
            paint.setStyle(Paint.Style.FILL);
            drawText(canvas, paint, text, rect.centerX(), rect.centerY() + 6, 17,
                    primary ? Color.WHITE : Color.rgb(174, 199, 214),
                    Paint.Align.CENTER, true);
        }

        @Override
        public boolean onTouchEvent(MotionEvent event)
        {
            final float x = event.getX() * DESIGN_WIDTH / Math.max(1, getWidth());
            final float y = event.getY() * DESIGN_HEIGHT / Math.max(1, getHeight());
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN)
            {
                mTouchDownY = y;
                return true;
            }
            if (event.getActionMasked() != MotionEvent.ACTION_UP)
            {
                return true;
            }

            final float swipe = y - mTouchDownY;
            if (Math.abs(swipe) > 80.0f)
            {
                moveSelection(swipe < 0.0f ? VISIBLE_ROWS : -VISIBLE_ROWS);
                return true;
            }

            for (int visible = 0; visible < VISIBLE_ROWS; ++visible)
            {
                final int index = mFirstVisible + visible;
                if (index >= CONTROL_NAMES.length)
                {
                    break;
                }
                final float top = ROW_TOP + visible * (ROW_HEIGHT + ROW_GAP);
                if (x >= 170 && x <= 1430 && y >= top && y <= top + ROW_HEIGHT)
                {
                    mSelected = index;
                    ensureSelectedVisible();
                    beginCapture();
                    performClick();
                    return true;
                }
            }

            if (RESET_RECT.contains(x, y))
            {
                resetToDefault();
            }
            else if (CLEAR_RECT.contains(x, y))
            {
                clearSelected();
            }
            else if (DONE_RECT.contains(x, y))
            {
                mDialog.dismiss();
            }
            performClick();
            return true;
        }

        @Override
        public boolean performClick()
        {
            super.performClick();
            return true;
        }

        private String mappedName(int command)
        {
            final String mapped = mMap.getMappedCodeInfo(command);
            if (TextUtils.isEmpty(mapped))
            {
                return "NOT MAPPED";
            }
            final int newline = mapped.indexOf('\n');
            final String first = newline >= 0 ? mapped.substring(0, newline) : mapped;
            return friendlyProviderName(first);
        }

        private static String friendlyInputName(int inputCode)
        {
            if (inputCode < 0)
            {
                final int axis = (-inputCode - 1) / 2;
                final boolean positive = ((-inputCode) % 2) == 1;
                switch (axis)
                {
                    case MotionEvent.AXIS_X:
                        return "Left Stick " + (positive ? "Right" : "Left");
                    case MotionEvent.AXIS_Y:
                        return "Left Stick " + (positive ? "Down" : "Up");
                    case MotionEvent.AXIS_Z:
                        return "Right Stick " + (positive ? "Right" : "Left");
                    case MotionEvent.AXIS_RZ:
                        return "Right Stick " + (positive ? "Down" : "Up");
                    case MotionEvent.AXIS_HAT_X:
                        return "D-Pad " + (positive ? "Right" : "Left");
                    case MotionEvent.AXIS_HAT_Y:
                        return "D-Pad " + (positive ? "Down" : "Up");
                    case MotionEvent.AXIS_LTRIGGER:
                    case MotionEvent.AXIS_BRAKE:
                        return "Left Trigger";
                    case MotionEvent.AXIS_RTRIGGER:
                    case MotionEvent.AXIS_GAS:
                        return "Right Trigger";
                    default:
                        return friendlyProviderName(AbstractProvider.getInputName(inputCode));
                }
            }
            return friendlyProviderName(AbstractProvider.getInputName(inputCode));
        }

        private static String friendlyProviderName(String name)
        {
            if (name == null)
            {
                return "NOT MAPPED";
            }
            return name.replace("KEYCODE_BUTTON_", "Quest ")
                    .replace("KEYCODE_", "")
                    .replace("AXIS_", "Axis ")
                    .replace('_', ' ');
        }

        private static boolean isControllerKey(int keyCode)
        {
            return keyCode == KeyEvent.KEYCODE_BACK ||
                    keyCode == KeyEvent.KEYCODE_ESCAPE ||
                    keyCode == KeyEvent.KEYCODE_DPAD_UP ||
                    keyCode == KeyEvent.KEYCODE_DPAD_DOWN ||
                    keyCode == KeyEvent.KEYCODE_DPAD_LEFT ||
                    keyCode == KeyEvent.KEYCODE_DPAD_RIGHT ||
                    keyCode == KeyEvent.KEYCODE_DPAD_CENTER ||
                    keyCode == KeyEvent.KEYCODE_ENTER ||
                    keyCode >= KeyEvent.KEYCODE_BUTTON_A;
        }

        private static float strongestAxis(MotionEvent event, int first, int second)
        {
            final float a = event.getAxisValue(first);
            final float b = event.getAxisValue(second);
            return Math.abs(a) >= Math.abs(b) ? a : b;
        }

        private static int axisToInputCode(int axisCode, boolean positiveDirection)
        {
            return -(axisCode * 2 + (positiveDirection ? 1 : 2));
        }
    }

    private static void drawText(Canvas canvas, Paint paint, String text, float x, float y,
            float size, int color, Paint.Align align, boolean bold)
    {
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
