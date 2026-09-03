package com.ezquest.engine.diag;

import android.app.ActivityManager;
import android.content.Context;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.io.StringWriter;

/**
 * Java-layer crash capture. Installs a default uncaught-exception handler
 * that writes a {@code crash-<epoch>.txt} report (build stamp, thread,
 * stack trace, memory snapshot) into the diagnostics dir, logs the event,
 * then chains to the previous handler so the process still dies loudly.
 *
 * This complements — not replaces — the native signal handler the engine
 * already installs ({@code InitCrashHandler} in launcher/android/main.cpp),
 * which covers native crashes this layer never sees.
 */
public final class CrashProbe {
    private static final String TAG = "EZQuest-Crash";
    private static boolean sArmed;

    private CrashProbe() {
    }

    public static synchronized boolean arm(Context context) {
        if (sArmed) {
            return true;
        }
        final Context appCtx = context.getApplicationContext();
        final Thread.UncaughtExceptionHandler previous =
                Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(
                new Thread.UncaughtExceptionHandler() {
                    @Override
                    public void uncaughtException(Thread thread, Throwable error) {
                        try {
                            File report = writeReport(appCtx, thread, error);
                            EventLog.get(appCtx).event("crash",
                                    "thread", thread.getName(),
                                    "error", String.valueOf(error),
                                    "file", report.getName());
                            Log.e(TAG, "crash captured: " + report.getAbsolutePath(), error);
                        } catch (Throwable t) {
                            Log.e(TAG, "crash report failed", t);
                        }
                        if (previous != null) {
                            previous.uncaughtException(thread, error);
                        }
                    }
                });
        sArmed = true;
        Log.i(TAG, "java crash capture armed");
        return true;
    }

    public static boolean isArmed() {
        return sArmed;
    }

    /** Crash reports waiting in the diagnostics dir. */
    public static int pendingReports(Context context) {
        return EventLog.get(context).countMatching("crash-", ".txt");
    }

    private static File writeReport(Context context, Thread thread, Throwable error)
            throws Exception {
        File file = new File(DiagnosticsDir.get(context),
                "crash-" + (System.currentTimeMillis() / 1000)
                        + "-" + BuildInfo.buildHash(context) + ".txt");
        StringBuilder sb = new StringBuilder(16384);
        sb.append("=== EZQuest crash report ===\n");
        String[] fields = BuildInfo.reportFields(context);
        for (int i = 0; i + 1 < fields.length; i += 2) {
            sb.append(fields[i]).append(": ").append(fields[i + 1]).append('\n');
        }
        sb.append("thread: ").append(thread.getName())
                .append(" (id=").append(thread.getId()).append(")\n");
        sb.append(memoryLine(context));
        sb.append("\n--- stack ---\n");
        StringWriter sw = new StringWriter();
        error.printStackTrace(new PrintWriter(sw));
        sb.append(sw).append("\n=== end ===\n");
        FileWriter writer = new FileWriter(file);
        try {
            writer.write(sb.toString());
        } finally {
            writer.close();
        }
        return file;
    }

    private static String memoryLine(Context context) {
        try {
            ActivityManager am =
                    (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            return "memory: totalMB=" + (mi.totalMem / 1048576L)
                    + " availMB=" + (mi.availMem / 1048576L)
                    + " lowMemory=" + mi.lowMemory
                    + " maxHeapBytes=" + Runtime.getRuntime().maxMemory() + "\n";
        } catch (Exception e) {
            return "memory: (unavailable: " + e + ")\n";
        }
    }
}
