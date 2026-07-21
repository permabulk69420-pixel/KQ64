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
 *
 * Mupen64PlusAE is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with Mupen64PlusAE. If
 * not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: littleguy77
 */
package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.app.Application;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.util.Base64;
import android.view.View;
import android.view.ViewGroup;

import androidx.activity.OnBackPressedCallback;
import androidx.appcompat.app.AppCompatActivity;
import androidx.multidex.MultiDexApplication;

import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;

/**
 * Application entry point plus the small amount of shell-only integration needed by the Quest
 * dashboard. The emulator renderer and GameActivity remain completely outside this class.
 */
public class AppMupen64Plus extends MultiDexApplication
{
    @Override
    public void onCreate()
    {
        super.onCreate();
        registerActivityLifecycleCallbacks(new Kq64LauncherIntegration());
    }

    private static final class Kq64LauncherIntegration
            implements Application.ActivityLifecycleCallbacks
    {
        private static final String TAG = "Kq64LauncherIntegration";
        private static final String EXTRA_ADVANCED_ONLY =
                "paulscode.android.mupen64plusae.extra.KQ64_ADVANCED_ONLY";

        private boolean mQuestLauncherSeen;

        /**
         * GalleryActivity treats a null extras bundle as a request to resume the previous game.
         * Mark the dashboard's Advanced route before GalleryActivity.onCreate so it opens as an
         * ordinary Android settings/library panel instead of re-entering immersive GameActivity.
         */
        @Override
        public void onActivityPreCreated(Activity activity, Bundle savedInstanceState)
        {
            if (!mQuestLauncherSeen || !(activity instanceof GalleryActivity))
            {
                return;
            }

            final Intent intent = activity.getIntent();
            if (intent != null && intent.getExtras() == null)
            {
                intent.putExtra(EXTRA_ADVANCED_ONLY, true);
                mQuestLauncherSeen = false;
                QuestVrDiagnostics.info(TAG,
                        "Marked legacy Gallery as KQ64 Advanced-only; game resume is suppressed");
            }
        }

        @Override
        public void onActivityCreated(Activity activity, Bundle savedInstanceState)
        {
            if (activity instanceof QuestVrLauncherActivity)
            {
                mQuestLauncherSeen = true;
                installDashboardBranding(activity);
                return;
            }

            final Intent intent = activity.getIntent();
            if (activity instanceof GalleryActivity && intent != null &&
                    intent.getBooleanExtra(EXTRA_ADVANCED_ONLY, false))
            {
                installDashboardReturn((AppCompatActivity) activity);
            }
        }

        private static void installDashboardBranding(Activity activity)
        {
            final View content = activity.findViewById(android.R.id.content);
            if (!(content instanceof ViewGroup))
            {
                QuestVrDiagnostics.warn(TAG, "Unable to find dashboard content for KQ64 branding");
                return;
            }

            final Kq64BrandDrawable branding = new Kq64BrandDrawable();
            content.getOverlay().add(branding);
            final Runnable updateBounds = () -> {
                branding.setBounds(0, 0, content.getWidth(), content.getHeight());
                content.invalidate();
            };
            content.addOnLayoutChangeListener((view, left, top, right, bottom,
                    oldLeft, oldTop, oldRight, oldBottom) -> updateBounds.run());
            content.post(updateBounds);
            QuestVrDiagnostics.info(TAG, "Installed KQ64 dashboard logo overlay");
        }

        private static void installDashboardReturn(AppCompatActivity activity)
        {
            activity.getOnBackPressedDispatcher().addCallback(activity,
                    new OnBackPressedCallback(true)
                    {
                        @Override
                        public void handleOnBackPressed()
                        {
                            setEnabled(false);
                            final Intent launcher =
                                    new Intent(activity, QuestVrLauncherActivity.class);
                            launcher.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP |
                                    Intent.FLAG_ACTIVITY_SINGLE_TOP);
                            activity.startActivity(launcher);
                            activity.finish();
                            activity.overridePendingTransition(0, 0);
                        }
                    });
            QuestVrDiagnostics.info(TAG,
                    "Advanced/Legacy Back now returns to the KQ64 dashboard");
        }

        @Override
        public void onActivityStarted(Activity activity)
        {
        }

        @Override
        public void onActivityResumed(Activity activity)
        {
        }

        @Override
        public void onActivityPaused(Activity activity)
        {
        }

        @Override
        public void onActivityStopped(Activity activity)
        {
        }

        @Override
        public void onActivitySaveInstanceState(Activity activity, Bundle outState)
        {
        }

        @Override
        public void onActivityDestroyed(Activity activity)
        {
        }
    }

    /**
     * Input-transparent artwork drawn through ViewOverlay, so Quest pointer and controller events
     * continue to reach the existing dashboard Canvas unchanged.
     */
    private static final class Kq64BrandDrawable extends Drawable
    {
        private static final int DESIGN_WIDTH = 1600;
        private static final int DESIGN_HEIGHT = 900;
        private static final String LOGO_BASE64 =
            "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" +
            "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";

        private static Bitmap sLogo;
        private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG |
                Paint.FILTER_BITMAP_FLAG);

        @Override
        public void draw(Canvas canvas)
        {
            if (getBounds().width() <= 0 || getBounds().height() <= 0)
            {
                return;
            }

            canvas.save();
            canvas.translate(getBounds().left, getBounds().top);
            canvas.scale(getBounds().width() / (float) DESIGN_WIDTH,
                    getBounds().height() / (float) DESIGN_HEIGHT);

            // Opaque shell-coloured patch removes the inherited Mupen title without changing the
            // rest of the dashboard or its hit regions.
            mPaint.setStyle(Paint.Style.FILL);
            mPaint.setShader(new LinearGradient(0, 0, DESIGN_WIDTH, DESIGN_HEIGHT,
                    Color.rgb(2, 6, 17), Color.rgb(5, 24, 48), Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, 620, 146, mPaint);
            mPaint.setShader(null);

            final Bitmap logo = getLogo();
            if (logo != null)
            {
                canvas.drawBitmap(logo, null, new RectF(18, 5, 158, 145), mPaint);
            }

            drawText(canvas, "KQ64", 178, 86, 49, Color.WHITE, true);
            drawText(canvas, "QUEST LIBRARY", 181, 121, 17,
                    Color.rgb(54, 202, 255), true);
            canvas.restore();
        }

        private void drawText(Canvas canvas, String text, float x, float y, float size,
                int color, boolean bold)
        {
            mPaint.setShader(null);
            mPaint.setStyle(Paint.Style.FILL);
            mPaint.setColor(color);
            mPaint.setTextSize(size);
            mPaint.setTextAlign(Paint.Align.LEFT);
            mPaint.setTypeface(bold ? android.graphics.Typeface.DEFAULT_BOLD :
                    android.graphics.Typeface.DEFAULT);
            canvas.drawText(text, x, y, mPaint);
        }

        private static Bitmap getLogo()
        {
            if (sLogo == null)
            {
                final byte[] bytes = Base64.decode(LOGO_BASE64, Base64.DEFAULT);
                sLogo = BitmapFactory.decodeByteArray(bytes, 0, bytes.length);
            }
            return sLogo;
        }

        @Override
        public void setAlpha(int alpha)
        {
            mPaint.setAlpha(alpha);
        }

        @Override
        public void setColorFilter(android.graphics.ColorFilter colorFilter)
        {
            mPaint.setColorFilter(colorFilter);
        }

        @Override
        public int getOpacity()
        {
            return PixelFormat.TRANSLUCENT;
        }
    }
}
