package paulscode.android.mupen64plusae;

import android.app.Activity;
import android.app.ActivityManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Handler;
import android.os.Process;
import android.os.SystemClock;

import java.util.List;

import paulscode.android.mupen64plusae.questvr.QuestVrBridge;
import paulscode.android.mupen64plusae.questvr.QuestVrDiagnostics;

/**
 * Explicit reset endpoint hosted in {@code :EmulationProcess}.
 *
 * GameActivity, CoreService, and their SharedPreferences cache live in that separate process.
 * Android is allowed to retain the empty process after a game closes, which can otherwise carry
 * stale settings or native/OpenXR state into the next launch. The launcher invokes this receiver
 * only when that old process is already present, then waits for its confirmed disappearance before
 * creating GameActivity again.
 */
public final class QuestVrProcessResetReceiver extends BroadcastReceiver {
    static final String ACTION_RESET_EMULATION_PROCESS =
            "paulscode.android.mupen64plusae.action.RESET_QUEST_EMULATION_PROCESS";
    private static final String TAG = "QuestVrProcessReset";
    private static final long PROCESS_RESET_TIMEOUT_MS = 4000L;
    private static final long PROCESS_POLL_INTERVAL_MS = 50L;

    static void prepareForQuestLaunch(Activity activity, Handler handler, Runnable ready,
            Runnable failure) {
        if (!QuestVrBridge.shouldLaunchVrLauncher(activity) ||
                !isEmulationProcessRunning(activity)) {
            ready.run();
            return;
        }

        QuestVrDiagnostics.info(TAG,
                "Resetting retained :EmulationProcess before immersive launch");
        final Intent resetIntent = new Intent(activity, QuestVrProcessResetReceiver.class);
        resetIntent.setAction(ACTION_RESET_EMULATION_PROCESS);
        activity.sendBroadcast(resetIntent);
        waitForExit(activity, handler, ready, failure,
                SystemClock.uptimeMillis() + PROCESS_RESET_TIMEOUT_MS);
    }

    private static void waitForExit(Activity activity, Handler handler, Runnable ready,
            Runnable failure, long deadline) {
        if (activity.isFinishing() || activity.isDestroyed()) {
            failure.run();
            return;
        }
        if (!isEmulationProcessRunning(activity)) {
            ready.run();
            return;
        }
        if (SystemClock.uptimeMillis() >= deadline) {
            QuestVrDiagnostics.warn(TAG,
                    "Timed out waiting for stale :EmulationProcess; launch cancelled safely");
            failure.run();
            return;
        }
        handler.postDelayed(() -> waitForExit(activity, handler, ready, failure, deadline),
                PROCESS_POLL_INTERVAL_MS);
    }

    private static boolean isEmulationProcessRunning(Context context) {
        final ActivityManager manager =
                (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
        final List<ActivityManager.RunningAppProcessInfo> processes =
                manager == null ? null : manager.getRunningAppProcesses();
        if (processes == null) {
            return false;
        }
        final String processName = context.getPackageName() + ":EmulationProcess";
        for (ActivityManager.RunningAppProcessInfo process : processes) {
            if (processName.equals(process.processName)) {
                return true;
            }
        }
        return false;
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION_RESET_EMULATION_PROCESS.equals(intent.getAction())) {
            return;
        }
        QuestVrDiagnostics.info(TAG,
                "Terminating retained :EmulationProcess before the next game launch");
        Process.killProcess(Process.myPid());
    }
}
