package paulscode.android.mupen64plusae;

import android.util.Log;

import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/**
 * Diagnostic launcher that removes profile ambiguity from the Quest VR bring-up.
 *
 * The upstream emulator defaults to Glide64-Accurate, while the stereo geometry bridge is compiled
 * into GLideN64. Before launching a ROM this activity enables Quest VR and synchronously selects the
 * built-in GlideN64-Fast profile for that game.
 */
public final class QuestVrTestGalleryActivity extends GalleryActivity {
    private static final String TAG = "QuestVrTestLauncher";
    private static final String GLIDEN64_VR_PROFILE = "GlideN64-Fast";

    @Override
    void startGameActivity(String romPath, String zipPath, String romMd5, String romCrc,
            String romHeaderName, byte romCountryCode, String romArtPath, String romGoodName,
            String romDisplayName, boolean doRestart, boolean isNetplayEnabled,
            boolean isNetplayServer) {
        final boolean vrSaved = getSharedPreferences(QuestVrSettings.PREFERENCES_NAME, MODE_PRIVATE)
                .edit()
                .putBoolean("enabled", true)
                .commit();
        final String gamePreferencesName = romMd5.replace(' ', '_') + "_preferences";
        final boolean profileSaved = getSharedPreferences(gamePreferencesName, MODE_PRIVATE)
                .edit()
                .putString("emulationProfile", GLIDEN64_VR_PROFILE)
                .commit();
        Log.i(TAG, "Launching controlled Quest VR test; enabled=" + vrSaved +
                " profile=" + GLIDEN64_VR_PROFILE + " saved=" + profileSaved +
                " rom=" + romHeaderName);

        super.startGameActivity(romPath, zipPath, romMd5, romCrc, romHeaderName,
                romCountryCode, romArtPath, romGoodName, romDisplayName, doRestart,
                isNetplayEnabled, isNetplayServer);
    }
}
