/*
 * Mupen64PlusAE, an N64 emulator for the Android platform
 *
 * This file is part of Mupen64PlusAE and is distributed under the GNU GPL v3 or later.
 */
package paulscode.android.mupen64plusae.persistent;

import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.widget.Toast;

import androidx.core.content.FileProvider;
import androidx.preference.Preference;

import java.io.File;

import paulscode.android.mupen64plusae.R;
import paulscode.android.mupen64plusae.compat.AppCompatPreferenceActivity;
import paulscode.android.mupen64plusae.compat.AppCompatPreferenceFragment;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;
import paulscode.android.mupen64plusae.questvr.QuestVrSettings;

/** Developer-facing controls for the optional Quest/OpenXR prototype. */
public class QuestVrPrefsActivity extends AppCompatPreferenceActivity {
    private static final String EXPORT_DIAGNOSTICS_KEY = "export_diagnostics";

    @Override
    protected String getSharedPrefsName() {
        return QuestVrSettings.PREFERENCES_NAME;
    }

    @Override
    protected int getSharedPrefsId() {
        return R.xml.preferences_quest_vr;
    }

    @Override
    public void onFragmentCreation(AppCompatPreferenceFragment currentFragment) {
        super.onFragmentCreation(currentFragment);
        final Preference export = currentFragment.findPreference(EXPORT_DIAGNOSTICS_KEY);
        if (export != null) {
            export.setOnPreferenceClickListener(preference -> {
                shareLatestDiagnostics();
                return true;
            });
        }
    }

    private void shareLatestDiagnostics() {
        final File log = QuestVrDiagnostics.getLatestLogFile(this);
        if (!log.isFile() || log.length() == 0) {
            Toast.makeText(this, R.string.questVrExport_empty, Toast.LENGTH_LONG).show();
            return;
        }

        try {
            final Uri uri = FileProvider.getUriForFile(this,
                    getPackageName() + ".filesprovider", log);
            final Intent share = new Intent(Intent.ACTION_SEND);
            share.setType("text/plain");
            share.putExtra(Intent.EXTRA_SUBJECT, "Mupen64Plus AE Quest/OpenXR diagnostic");
            share.putExtra(Intent.EXTRA_STREAM, uri);
            share.setClipData(ClipData.newRawUri(log.getName(), uri));
            share.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivity(Intent.createChooser(share,
                    getString(R.string.questVrExport_chooser)));
            QuestVrDiagnostics.info("QuestVrPrefsActivity",
                    "Opened share sheet for diagnostic log bytes=" + log.length());
        } catch (RuntimeException error) {
            QuestVrDiagnostics.error("QuestVrPrefsActivity",
                    "Unable to share diagnostic log " + log, error);
            Toast.makeText(this, R.string.questVrExport_failed, Toast.LENGTH_LONG).show();
        }
    }
}
