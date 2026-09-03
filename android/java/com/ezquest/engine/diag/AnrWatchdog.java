package com.ezquest.engine.diag;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.Locale;
import java.util.Map;

/**
 * Watches for application stalls (ANR precursors) and captures a report.
 *
 * Two independent signals: a token posted to the main thread every poll
 * (late token == main thread stuck), and an optional native frame-loop
 * heartbeat file ({@code diagnostics/heartbeat.bin}, one little-endian
 * long counter written by native code in slice D; absent == Java-only
 * mode). A report is written at most once per rearm window.
 */
public final class AnrWatchdog {
    /** Main thread unresponsive past this counts as a stall. */
    public static final long TIMEOUT_MS = 3000;
    private static final long POLL_MS = 500;
    private static final long REARM_MS = 30000;
    private static final String HEARTBEAT_NAME = "heartbeat.bin";
    private static final String TAG = "EZQuest-ANR";

    private static AnrWatchdog sInstance;

    private final Context mCtx;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private volatile boolean mRunning;
    private Thread mThread;
    private volatile long mTokenPosted;
    private volatile long mTokenSeen;
    private long mLastNativeCounter = -1;
    private long mLastNativeChangeMs;
    private long mLastReportMs;
    private int mStallCount;

    private AnrWatchdog(Context context) {
        mCtx = context.getApplicationContext();
    }

    public static synchronized AnrWatchdog get(Context context) {
        if (sInstance == null) {
            sInstance = new AnrWatchdog(context);
        }
        return sInstance;
    }

    public int stallCount() {
        return mStallCount;
    }

    public synchronized void start() {
        if (mRunning) {
            return;
        }
        mRunning = true;
        mLastNativeChangeMs = SystemClock.uptimeMillis();
        Thread thread = new Thread(new Runnable() {
            @Override
            public void run() {
                loop();
            }
        }, "ezquest-anr-watchdog");
        mThread = thread;
        thread.setDaemon(true);
        thread.start();
        Log.i(TAG, "watchdog armed (main-thread token + optional native heartbeat)");
    }

    public synchronized void stop() {
        mRunning = false;
        if (mThread != null) {
            mThread.interrupt();
            mThread = null;
        }
    }

    private void loop() {
        while (mRunning) {
            final long token = mTokenPosted + 1;
            mTokenPosted = token;
            mMain.post(new Runnable() {
                @Override
                public void run() {
                    mTokenSeen = token;
                }
            });
            long postedAt = SystemClock.uptimeMillis();
            sleepQuietly(POLL_MS);
            if (!mRunning) {
                return;
            }
            long javaLateMs = 0;
            if (mTokenSeen < token) {
                while (mRunning && mTokenSeen < token
                        && SystemClock.uptimeMillis() - postedAt < TIMEOUT_MS) {
                    sleepQuietly(POLL_MS);
                }
                if (mRunning && mTokenSeen < token) {
                    javaLateMs = SystemClock.uptimeMillis() - postedAt;
                }
            }
            long nativeLateMs = heartbeatLateMs();
            if (javaLateMs >= TIMEOUT_MS || nativeLateMs >= TIMEOUT_MS) {
                maybeReport(javaLateMs, nativeLateMs);
            }
        }
    }

    /**
     * @return ms since the native counter last moved, 0 when healthy or
     *         when no heartbeat file is wired up yet.
     */
    private long heartbeatLateMs() {
        File file = new File(DiagnosticsDir.get(mCtx), HEARTBEAT_NAME);
        if (!file.isFile()) {
            return 0;
        }
        long counter = readCounter(file);
        long now = SystemClock.uptimeMillis();
        if (counter != mLastNativeCounter) {
            mLastNativeCounter = counter;
            mLastNativeChangeMs = now;
            return 0;
        }
        if (counter <= 0) {
            return 0;
        }
        return now - mLastNativeChangeMs;
    }

    private long readCounter(File file) {
        FileInputStream in = null;
        try {
            in = new FileInputStream(file);
            byte[] buf = new byte[8];
            int read = 0;
            while (read < buf.length) {
                int n = in.read(buf, read, buf.length - read);
                if (n <= 0) {
                    break;
                }
                read += n;
            }
            if (read < buf.length) {
                return mLastNativeCounter;
            }
            return ByteBuffer.wrap(buf).order(ByteOrder.LITTLE_ENDIAN).getLong();
        } catch (Exception ignored) {
            return mLastNativeCounter;
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private void maybeReport(long javaLateMs, long nativeLateMs) {
        long now = SystemClock.uptimeMillis();
        if (now - mLastReportMs < REARM_MS) {
            return;
        }
        mLastReportMs = now;
        mStallCount++;
        writeReport(javaLateMs, nativeLateMs);
    }

    private void writeReport(long javaLateMs, long nativeLateMs) {
        File file = new File(DiagnosticsDir.get(mCtx),
                "anr-" + (System.currentTimeMillis() / 1000)
                        + "-" + BuildInfo.buildHash(mCtx) + ".txt");
        StringBuilder sb = new StringBuilder(32768);
        sb.append("=== EZQuest ANR report ===\n");
        appendFields(sb);
        sb.append("timeout_ms: ").append(TIMEOUT_MS).append('\n');
        sb.append("java_main_late_ms: ").append(javaLateMs).append('\n');
        sb.append("native_frame_late_ms: ").append(nativeLateMs).append('\n');
        sb.append("native_frame_counter: ");
        sb.append(mLastNativeCounter < 0 ? "<not wired>" : String.valueOf(mLastNativeCounter));
        sb.append('\n');
        sb.append("stall_index: ").append(mStallCount).append('\n');
        sb.append("which: ");
        if (javaLateMs >= TIMEOUT_MS) {
            sb.append(nativeLateMs >= TIMEOUT_MS ? "java-main+native-frameloop" : "java-main");
        } else {
            sb.append("native-frameloop");
        }
        sb.append('\n');
        sb.append("uptime_ms: ").append(SystemClock.uptimeMillis()).append("\n\n");
        appendJavaThreads(sb);
        appendNativeTasks(sb);
        sb.append("=== end ===\n");
        FileWriter writer = null;
        try {
            writer = new FileWriter(file);
            writer.write(sb.toString());
            Log.w(TAG, "ANR precursor captured: " + file.getAbsolutePath()
                    + " java_late=" + javaLateMs + "ms native_late=" + nativeLateMs + "ms");
            EventLog.get(mCtx).event("anr_precursor",
                    "java_late_ms", String.valueOf(javaLateMs),
                    "native_late_ms", String.valueOf(nativeLateMs),
                    "file", file.getName());
        } catch (Exception e) {
            Log.e(TAG, "could not write " + file, e);
        } finally {
            if (writer != null) {
                try {
                    writer.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private void appendFields(StringBuilder sb) {
        String[] fields = BuildInfo.reportFields(mCtx);
        for (int i = 0; i + 1 < fields.length; i += 2) {
            sb.append(fields[i]).append(": ").append(fields[i + 1]).append('\n');
        }
    }

    private void appendJavaThreads(StringBuilder sb) {
        sb.append("--- java threads ---\n");
        Thread main = Looper.getMainLooper().getThread();
        try {
            for (Map.Entry<Thread, StackTraceElement[]> entry
                    : Thread.getAllStackTraces().entrySet()) {
                Thread thread = entry.getKey();
                sb.append(String.format(Locale.US, "\"%s\" tid=%d state=%s%s\n",
                        thread.getName(), thread.getId(), thread.getState(),
                        thread == main ? "  <-- MAIN" : ""));
                for (StackTraceElement frame : entry.getValue()) {
                    sb.append("    at ").append(frame).append('\n');
                }
            }
        } catch (Throwable t) {
            sb.append("(unavailable: ").append(t).append(")\n");
        }
    }

    private void appendNativeTasks(StringBuilder sb) {
        sb.append("\n--- native threads (/proc/self/task) ---\n");
        try {
            File[] tasks = new File("/proc/self/task").listFiles();
            if (tasks == null) {
                sb.append("(unavailable)\n");
                return;
            }
            for (File task : tasks) {
                String comm = readSmall(new File(task, "comm")).trim();
                String stat = readSmall(new File(task, "stat")).trim();
                String wchan = readSmall(new File(task, "wchan")).trim();
                sb.append(String.format(Locale.US, "tid %-7s %-16s state=%s wchan=%s\n",
                        task.getName(), comm, threadState(stat),
                        wchan.isEmpty() ? "?" : wchan));
            }
        } catch (Throwable t) {
            sb.append("(unavailable: ").append(t).append(")\n");
        }
    }

    private static String threadState(String stat) {
        int close = stat.lastIndexOf(')');
        if (close > 0 && close + 2 < stat.length()) {
            return stat.substring(close + 2, close + 3);
        }
        return "?";
    }

    private static String readSmall(File file) {
        FileInputStream in = null;
        try {
            in = new FileInputStream(file);
            byte[] buf = new byte[8192];
            int read = in.read(buf);
            return read <= 0 ? "" : new String(buf, 0, read);
        } catch (Exception ignored) {
            return "";
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    private static void sleepQuietly(long ms) {
        try {
            Thread.sleep(ms);
        } catch (InterruptedException ignored) {
        }
    }
}
