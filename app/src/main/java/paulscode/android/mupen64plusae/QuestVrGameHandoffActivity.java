package paulscode.android.mupen64plusae;

import android.content.Intent;
import android.os.Bundle;

import androidx.appcompat.app.AppCompatActivity;

import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;

/**
 * Compatibility redirect for task stacks created by the withdrawn OpenXR-to-OpenXR handoff.
 * New launches never enter this Activity, but keeping the component allows an upgraded install to
 * recover cleanly if Android tries to restore an old handoff task from recents.
 */
@Deprecated
public final class QuestVrGameHandoffActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        QuestVrDiagnostics.info("QuestVrGameHandoff",
                "Redirecting obsolete handoff task to the Android-panel launcher");
        ActivityHelper.startMainActivity(this, new Intent());
        finish();
        overridePendingTransition(0, 0);
    }
}
