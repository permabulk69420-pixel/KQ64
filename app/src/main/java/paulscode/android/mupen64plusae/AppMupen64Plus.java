package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.app.Application;
import android.content.Intent;
import android.os.Bundle;

import androidx.activity.OnBackPressedCallback;
import androidx.appcompat.app.AppCompatActivity;
import androidx.multidex.MultiDexApplication;

/** Application-level routing for the KQ64 Quest launcher. */
public class AppMupen64Plus extends MultiDexApplication {
    private static final String EXTRA_KQ64_ADVANCED =
            "paulscode.android.mupen64plusae.extra.KQ64_ADVANCED";

    @Override
    public void onCreate() {
        super.onCreate();
        registerActivityLifecycleCallbacks(new Kq64LauncherRoutes());
    }

    private static final class Kq64LauncherRoutes
            implements Application.ActivityLifecycleCallbacks {
        private boolean mLauncherWasLastActivity;

        @Override
        public void onActivityPreCreated(Activity activity, Bundle savedInstanceState) {
            if (activity instanceof GalleryActivity && mLauncherWasLastActivity) {
                final Intent intent = activity.getIntent();
                if (intent != null && intent.getExtras() == null) {
                    intent.putExtra(EXTRA_KQ64_ADVANCED, true);
                }
                mLauncherWasLastActivity = false;
            } else if (!(activity instanceof QuestVrLauncherActivity)) {
                mLauncherWasLastActivity = false;
            }
        }

        @Override
        public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
            if (activity instanceof QuestVrLauncherActivity) {
                Kq64ControllerMapping.installLauncherButton((AppCompatActivity) activity);
                return;
            }

            final Intent intent = activity.getIntent();
            if (!(activity instanceof GalleryActivity) || intent == null ||
                    !intent.getBooleanExtra(EXTRA_KQ64_ADVANCED, false)) {
                return;
            }

            final AppCompatActivity gallery = (AppCompatActivity) activity;
            gallery.getOnBackPressedDispatcher().addCallback(gallery,
                    new OnBackPressedCallback(true) {
                        @Override
                        public void handleOnBackPressed() {
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
        public void onActivityResumed(Activity activity) {
            mLauncherWasLastActivity = activity instanceof QuestVrLauncherActivity;
        }

        @Override
        public void onActivityStarted(Activity activity) {
        }

        @Override
        public void onActivityPaused(Activity activity) {
        }

        @Override
        public void onActivityStopped(Activity activity) {
        }

        @Override
        public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
        }

        @Override
        public void onActivityDestroyed(Activity activity) {
        }
    }
}
