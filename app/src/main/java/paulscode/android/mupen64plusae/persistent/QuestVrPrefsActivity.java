/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * This file is part of Mupen64PlusAE and is distributed under the GNU GPL v3 or later.
 */
package paulscode.android.mupen64plusae.persistent;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/** Developer-facing controls for the optional Quest/OpenXR prototype. */
public class QuestVrPrefsActivity extends AppCompatPreferenceActivity {
    @Override
    protected String getSharedPrefsName() {
        return QuestVrSettings.PREFERENCES_NAME;
    }

    @Override
    protected int getSharedPrefsId() {
        return R.xml.preferences_quest_vr;
    }
}
