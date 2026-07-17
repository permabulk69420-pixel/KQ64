package paulscode.android.mupen64plusae.questvr;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.util.Log;

import java.io.File;

/**
 * Makes the Quest prototype use the renderer that actually contains the stereo geometry bridge.
 *
 * The upstream application defaults to Glide64-Accurate and may also retain an older per-game
 * profile. Starting OpenXR while another video plugin is selected gives us a valid immersive
 * session wrapped around the wrong renderer, which makes bring-up failures needlessly ambiguous.
 * This provider runs before the gallery activity, updates the global default synchronously, and
 * normalizes any existing per-game overrides.
 */
public final class QuestVrProfileInitializer extends ContentProvider {
    private static final String TAG = "QuestVrProfileInit";
    private static final String GLIDEN64_PROFILE = "GlideN64-Fast";
    private static final String GLOBAL_PROFILE_KEY = "emulationProfileDefault";
    private static final String GAME_PROFILE_KEY = "emulationProfile";

    @Override
    public boolean onCreate() {
        final Context context = getContext();
        if (context == null) {
            return false;
        }

        // AndroidX PreferenceManager uses this conventional name unless an application explicitly
        // overrides it. Avoid depending on AndroidX from this small native-support library module.
        final String defaultPreferencesName = context.getPackageName() + "_preferences";
        final boolean defaultSaved = context.getSharedPreferences(defaultPreferencesName,
                        Context.MODE_PRIVATE)
                .edit()
                .putString(GLOBAL_PROFILE_KEY, GLIDEN64_PROFILE)
                .commit();

        int gameOverridesUpdated = 0;
        final File preferencesDirectory = new File(context.getApplicationInfo().dataDir,
                "shared_prefs");
        final File[] preferenceFiles = preferencesDirectory.listFiles((directory, name) ->
                name.endsWith(".xml"));
        if (preferenceFiles != null) {
            for (File preferenceFile : preferenceFiles) {
                final String fileName = preferenceFile.getName();
                final String preferencesName = fileName.substring(0, fileName.length() - 4);
                final SharedPreferences preferences = context.getSharedPreferences(preferencesName,
                        Context.MODE_PRIVATE);
                if (!preferences.contains(GAME_PROFILE_KEY)) {
                    continue;
                }
                if (preferences.edit().putString(GAME_PROFILE_KEY, GLIDEN64_PROFILE).commit()) {
                    gameOverridesUpdated++;
                }
            }
        }

        Log.i(TAG, "Forced Quest VR renderer profile=" + GLIDEN64_PROFILE +
                " defaultSaved=" + defaultSaved +
                " gameOverridesUpdated=" + gameOverridesUpdated);
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
            String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
            String[] selectionArgs) {
        return 0;
    }
}
