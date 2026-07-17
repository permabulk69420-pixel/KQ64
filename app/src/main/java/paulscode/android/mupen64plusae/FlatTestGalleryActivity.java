package paulscode.android.mupen64plusae;

import android.content.Intent;
import android.util.Log;

import paulscode.android.mupen64plusae.game.GameActivity;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/**
 * Diagnostic launcher that starts the ordinary Android emulator path with Quest/OpenXR disabled.
 *
 * The normal prototype launcher deliberately marks GameActivity as an immersive-HMD activity.
 * That made the existing "Enable Quest VR" checkbox unsuitable for proving whether the underlying
 * emulator, ROM and emulation profile work independently of OpenXR. This launcher performs a
 * synchronous preference write and starts the explicit GameActivity without the immersive action
 * or category.
 */
public final class FlatTestGalleryActivity extends GalleryActivity {
    private static final String TAG = "FlatTestLauncher";

    @Override
    void startGameActivity(String romPath, String zipPath, String romMd5, String romCrc,
            String romHeaderName, byte romCountryCode, String romArtPath, String romGoodName,
            String romDisplayName, boolean doRestart, boolean isNetplayEnabled,
            boolean isNetplayServer) {
        final boolean saved = getSharedPreferences(QuestVrSettings.PREFERENCES_NAME, MODE_PRIVATE)
                .edit()
                .putBoolean("enabled", false)
                .commit();
        Log.i(TAG, "Launching controlled flat test; Quest VR disabled synchronously=" + saved);

        final Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra(ActivityHelper.Keys.ROM_PATH, romPath);
        intent.putExtra(ActivityHelper.Keys.ZIP_PATH, zipPath);
        intent.putExtra(ActivityHelper.Keys.ROM_MD5, romMd5);
        intent.putExtra(ActivityHelper.Keys.ROM_CRC, romCrc);
        intent.putExtra(ActivityHelper.Keys.ROM_HEADER_NAME, romHeaderName);
        intent.putExtra(ActivityHelper.Keys.ROM_COUNTRY_CODE, romCountryCode);
        intent.putExtra(ActivityHelper.Keys.ROM_ART_PATH, romArtPath);
        intent.putExtra(ActivityHelper.Keys.ROM_GOOD_NAME, romGoodName);
        intent.putExtra(ActivityHelper.Keys.ROM_DISPLAY_NAME, romDisplayName);
        intent.putExtra(ActivityHelper.Keys.DO_RESTART, doRestart);
        intent.putExtra(ActivityHelper.Keys.NETPLAY_ENABLED, isNetplayEnabled);
        intent.putExtra(ActivityHelper.Keys.NETPLAY_SERVER, isNetplayServer);
        mLaunchGame.launch(intent);
    }
}
