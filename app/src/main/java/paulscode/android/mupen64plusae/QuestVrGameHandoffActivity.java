package paulscode.android.mupen64plusae;

import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.view.WindowManager;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import java.lang.ref.WeakReference;

import paulscode.android.mupen64plusae.game.GameActivity;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/**
 * Plain Android lifecycle barrier between the launcher and game OpenXR Activities.
 *
 * OpenXR's Android instance information binds each instance to the Activity that drives its
 * lifecycle. Starting GameActivity directly from the still-active immersive launcher can leave
 * Quest transitioning between two Activity owners even after the first XrSession is destroyed.
 * This Activity deliberately has no immersive-HMD intent category and never loads or initializes
 * OpenXR. It waits until its ordinary window actually owns focus before entering the game, then
 * remains behind GameActivity so the reverse transition has the same clean boundary.
 */
public final class QuestVrGameHandoffActivity extends AppCompatActivity {
    private static final String TAG = "QuestVrGameHandoff";
    private static final String STATE_GAME_LAUNCHED = "gameLaunched";
    private static final String STATE_RETURN_PENDING = "returnPending";
    private static WeakReference<QuestVrGameHandoffActivity> sActiveInstance =
            new WeakReference<>(null);

    private boolean mGameLaunched;
    private boolean mReturnPending;
    private boolean mAdvancing;

    private final ActivityResultLauncher<Intent> mGameLauncher = registerForActivityResult(
            new ActivityResultContracts.StartActivityForResult(), result -> {
                QuestVrDiagnostics.info(TAG, "GameActivity returned result=" +
                        result.getResultCode());
                mReturnPending = true;
                mAdvancing = false;
                maybeAdvance();
            });

    static Intent createGameLaunchIntent(Context context, String romPath, String zipPath,
            String romMd5, String romCrc, String romHeaderName, byte romCountryCode,
            String romArtPath, String romGoodName, String romDisplayName,
            String presentationMode) {
        final Intent intent = new Intent(context, QuestVrGameHandoffActivity.class);
        intent.putExtra(ActivityHelper.Keys.ROM_PATH, romPath);
        intent.putExtra(ActivityHelper.Keys.ZIP_PATH, zipPath);
        intent.putExtra(ActivityHelper.Keys.ROM_MD5, romMd5);
        intent.putExtra(ActivityHelper.Keys.ROM_CRC, romCrc);
        intent.putExtra(ActivityHelper.Keys.ROM_HEADER_NAME, romHeaderName);
        intent.putExtra(ActivityHelper.Keys.ROM_COUNTRY_CODE, romCountryCode);
        intent.putExtra(ActivityHelper.Keys.ROM_ART_PATH, romArtPath);
        intent.putExtra(ActivityHelper.Keys.ROM_GOOD_NAME, romGoodName);
        intent.putExtra(ActivityHelper.Keys.ROM_DISPLAY_NAME, romDisplayName);
        intent.putExtra(ActivityHelper.Keys.DO_RESTART, false);
        intent.putExtra(ActivityHelper.Keys.NETPLAY_ENABLED, false);
        intent.putExtra(ActivityHelper.Keys.NETPLAY_SERVER, false);
        intent.putExtra(QuestVrSettings.EXTRA_PRESENTATION_MODE, presentationMode);
        return intent;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sActiveInstance = new WeakReference<>(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON |
                WindowManager.LayoutParams.FLAG_FULLSCREEN);
        final View barrier = new View(this);
        barrier.setBackgroundColor(Color.BLACK);
        setContentView(barrier);

        if (savedInstanceState != null) {
            mGameLaunched = savedInstanceState.getBoolean(STATE_GAME_LAUNCHED, false);
            mReturnPending = savedInstanceState.getBoolean(STATE_RETURN_PENDING, false);
        }
        QuestVrDiagnostics.info(TAG, "Created non-XR handoff gameLaunched=" +
                mGameLaunched + " returnPending=" + mReturnPending);
    }

    @Override
    protected void onPostResume() {
        super.onPostResume();
        maybeAdvance();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        QuestVrDiagnostics.info(TAG, "Window focus=" + hasFocus + " gameLaunched=" +
                mGameLaunched + " returnPending=" + mReturnPending);
        if (hasFocus) {
            maybeAdvance();
        }
    }

    @Override
    protected void onSaveInstanceState(@NonNull Bundle outState) {
        outState.putBoolean(STATE_GAME_LAUNCHED, mGameLaunched);
        outState.putBoolean(STATE_RETURN_PENDING, mReturnPending);
        super.onSaveInstanceState(outState);
    }

    @Override
    protected void onDestroy() {
        if (sActiveInstance.get() == this) {
            sActiveInstance.clear();
        }
        super.onDestroy();
    }

    static void onLauncherDestroyed() {
        final QuestVrGameHandoffActivity activity = sActiveInstance.get();
        if (activity != null) {
            activity.runOnUiThread(activity::maybeAdvance);
        }
    }

    private void maybeAdvance() {
        if (mAdvancing || isFinishing() || isDestroyed() || !hasWindowFocus()) {
            return;
        }
        if (!mGameLaunched && QuestVrLauncherActivity.isLifecycleOwnerAlive()) {
            QuestVrDiagnostics.info(TAG,
                    "Focused non-XR barrier is waiting for launcher Activity destruction");
            return;
        }
        if (mReturnPending) {
            returnToLauncher();
        } else if (!mGameLaunched) {
            launchGame();
        }
    }

    private void launchGame() {
        final Bundle extras = getIntent() == null ? null : getIntent().getExtras();
        final String romPath = extras == null ? null :
                extras.getString(ActivityHelper.Keys.ROM_PATH);
        final String romMd5 = extras == null ? null :
                extras.getString(ActivityHelper.Keys.ROM_MD5);
        if (TextUtils.isEmpty(romPath) || TextUtils.isEmpty(romMd5)) {
            QuestVrDiagnostics.warn(TAG,
                    "Handoff is missing ROM identity; returning to the launcher");
            mReturnPending = true;
            returnToLauncher();
            return;
        }

        mAdvancing = true;
        mGameLaunched = true;
        final Intent gameIntent = new Intent(this, GameActivity.class);
        ActivityHelper.configureQuestVrGameIntent(gameIntent);
        gameIntent.putExtras(extras);
        QuestVrDiagnostics.info(TAG,
                "Non-XR window focused; launching GameActivity without NEW_TASK");
        mGameLauncher.launch(gameIntent);
        overridePendingTransition(0, 0);
        mAdvancing = false;
    }

    private void returnToLauncher() {
        mAdvancing = true;
        QuestVrDiagnostics.info(TAG,
                "Non-XR window focused after game exit; returning to main launcher route");
        ActivityHelper.startMainActivity(this, new Intent());
        finish();
        overridePendingTransition(0, 0);
    }
}
