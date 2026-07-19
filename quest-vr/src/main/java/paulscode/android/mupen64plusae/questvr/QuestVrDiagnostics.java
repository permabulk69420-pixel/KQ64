package paulscode.android.mupen64plusae.questvr;

import android.content.Context;
import android.os.Build;
import android.os.Process;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Persistent, shareable diagnostics for the Quest/OpenXR presentation path. */
public final class QuestVrDiagnostics {
    private static final String TAG = "QuestVrDiagnostics";
    private static final String DIRECTORY = "quest-vr";
    private static final String FILE_NAME = "quest-vr-diagnostic-latest.log";
    private static final Object FILE_LOCK = new Object();
    private static volatile File sLogFile;

    private QuestVrDiagnostics() {
    }

    public static File startSession(Context context, String launchDescription) {
        synchronized (FILE_LOCK) {
            sLogFile = resolveLogFile(context);
            final String header = "=== Mupen64Plus AE Quest/OpenXR diagnostic session ===\n" +
                    "started=" + timestamp() + "\n" +
                    "launch=" + launchDescription + "\n" +
                    "device=" + Build.MANUFACTURER + " " + Build.MODEL +
                    " sdk=" + Build.VERSION.SDK_INT + " build=" + Build.DISPLAY + "\n" +
                    "process=" + Process.myPid() + " elapsedRealtimeMs=" +
                    SystemClock.elapsedRealtime() + "\n";
            writeBytes(sLogFile, header, false);
        }
        info("QuestVrDiagnostics", "Started diagnostic session at " + sLogFile.getAbsolutePath());
        return sLogFile;
    }

    public static File getLatestLogFile(Context context) {
        if (sLogFile == null) {
            synchronized (FILE_LOCK) {
                if (sLogFile == null) {
                    sLogFile = resolveLogFile(context);
                }
            }
        }
        return sLogFile;
    }

    public static void info(String component, String message) {
        append(Log.INFO, "I", component, message, null);
    }

    public static void warn(String component, String message) {
        append(Log.WARN, "W", component, message, null);
    }

    public static void error(String component, String message, Throwable throwable) {
        append(Log.ERROR, "E", component, message, throwable);
    }

    private static File resolveLogFile(Context context) {
        File directory = context.getExternalFilesDir(DIRECTORY);
        if (directory == null) {
            directory = new File(context.getFilesDir(), DIRECTORY);
        }
        if (!directory.exists() && !directory.mkdirs()) {
            Log.w(TAG, "Unable to create diagnostic directory " + directory);
        }
        return new File(directory, FILE_NAME);
    }

    private static void append(int logPriority, String level, String component, String message,
                               Throwable throwable) {
        final String safeMessage = message == null ? "(null)" : message;
        if (throwable == null) {
            Log.println(logPriority, component, safeMessage);
        } else {
            Log.e(component, safeMessage, throwable);
        }

        final File file = sLogFile;
        if (file == null) {
            return;
        }
        final String throwableText = throwable == null ? "" :
                " exception=" + throwable.getClass().getName() + ": " + throwable.getMessage();
        final String line = timestamp() + " " + level + " pid=" + Process.myPid() +
                " tid=" + Process.myTid() + " thread=" + Thread.currentThread().getName() +
                " [" + component + "] " + safeMessage.replace("\n", "\\n") + throwableText + "\n";
        synchronized (FILE_LOCK) {
            writeBytes(file, line, true);
        }
    }

    private static String timestamp() {
        return new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSSZ", Locale.US).format(new Date());
    }

    private static void writeBytes(File file, String text, boolean append) {
        try (FileOutputStream output = new FileOutputStream(file, append)) {
            output.write(text.getBytes(StandardCharsets.UTF_8));
            output.flush();
        } catch (IOException error) {
            Log.e(TAG, "Unable to write persistent diagnostics", error);
        }
    }
}
