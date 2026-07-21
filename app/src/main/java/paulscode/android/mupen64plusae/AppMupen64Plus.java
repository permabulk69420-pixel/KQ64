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
import android.os.Bundle;

import androidx.activity.OnBackPressedCallback;
import androidx.appcompat.app.AppCompatActivity;
import androidx.multidex.MultiDexApplication;

public class AppMupen64Plus extends MultiDexApplication
{
    private static final String EXTRA_KQ64_ADVANCED =
            "paulscode.android.mupen64plusae.extra.KQ64_ADVANCED";

    @Override
    public void onCreate()
    {
        super.onCreate();
        registerActivityLifecycleCallbacks(new Kq64AdvancedRoute());
    }

    /**
     * GalleryActivity historically treats an empty launch Intent as "resume the last game".
     * The KQ64 dashboard's Advanced button intentionally opens Gallery without ROM data, so mark
     * only that transition and provide a reliable Back route to the dashboard.
     */
    private static final class Kq64AdvancedRoute
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

        @Override
        public void onActivityResumed(Activity activity)
        {
            mLauncherWasLastActivity = activity instanceof QuestVrLauncherActivity;
        }

        @Override
        public void onActivityStarted(Activity activity)
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
}
