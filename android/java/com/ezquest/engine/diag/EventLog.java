package com.ezquest.engine.diag;

import android.content.Context;
import android.os.SystemClock;
import android.util.Log;

import org.json.JSONObject;

import java.io.File;
import java.io.FileWriter;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

/**
 * Append-only JSON-lines session log in the diagnostics dir, one file per
 * process start ({@code session-<epoch>.jsonl}).
 *
 * Old files are pruned on session start: anything older than 30 days goes,
 * then oldest non-report files until the dir is back under 200 MB. Crash
 * and ANR reports are pruned last so evidence survives pressure.
 */
public final class EventLog {
    /** 30 days. */
    public static final long MAX_AGE_MS = 30L * 24 * 60 * 60 * 1000;
    /** 200 MB. */
    public static final long MAX_BYTES = 200L * 1024 * 1024;

    private static final String TAG = "EZQuest-Diag";

    private static EventLog sInstance;

    private final Context mCtx;
    private final long mStartUptime = SystemClock.uptimeMillis();
    private final File mFile;
    private boolean mEnded;
    private int mEvents;

    private EventLog(Context context) {
        mCtx = context.getApplicationContext();
        mFile = new File(DiagnosticsDir.get(mCtx),
                "session-" + (System.currentTimeMillis() / 1000) + ".jsonl");
    }

    public static synchronized EventLog get(Context context) {
        if (sInstance == null) {
            sInstance = new EventLog(context);
        }
        return sInstance;
    }

    public File file() {
        return mFile;
    }

    public void sessionStart(String entryPoint) {
        prune();
        try {
            JSONObject obj = base("session_start");
            putFields(obj, BuildInfo.reportFields(mCtx));
            obj.put("entry_point", entryPoint);
            obj.put("pending_crash_reports", countMatching("crash-", ".txt"));
            obj.put("pending_anr_reports", countMatching("anr-", ".txt"));
            write(obj);
        } catch (Exception e) {
            Log.w(TAG, "sessionStart failed", e);
        }
    }

    public synchronized void sessionEnd(boolean cleanShutdown) {
        if (mEnded) {
            return;
        }
        mEnded = true;
        try {
            JSONObject obj = base("session_end");
            obj.put("clean_shutdown", cleanShutdown);
            obj.put("duration_ms", SystemClock.uptimeMillis() - mStartUptime);
            obj.put("events", mEvents);
            write(obj);
        } catch (Exception e) {
            Log.w(TAG, "sessionEnd failed", e);
        }
    }

    /** Log an event with flat key/value pairs. */
    public void event(String name, String... pairs) {
        try {
            JSONObject obj = base(name);
            putFields(obj, pairs);
            write(obj);
        } catch (Exception e) {
            Log.w(TAG, "event " + name + " failed", e);
        }
    }

    private JSONObject base(String name) throws Exception {
        JSONObject obj = new JSONObject();
        obj.put("t", System.currentTimeMillis());
        obj.put("uptime_ms", SystemClock.uptimeMillis() - mStartUptime);
        obj.put("event", name);
        obj.put("build_hash", BuildInfo.buildHash(mCtx));
        return obj;
    }

    private static void putFields(JSONObject obj, String[] pairs) throws Exception {
        for (int i = 0; i + 1 < pairs.length; i += 2) {
            obj.put(pairs[i], pairs[i + 1]);
        }
    }

    private synchronized void write(JSONObject obj) {
        mEvents++;
        FileWriter writer = null;
        try {
            writer = new FileWriter(mFile, true);
            writer.write(obj.toString());
            writer.write("\n");
        } catch (Exception e) {
            Log.w(TAG, "write failed", e);
        } finally {
            if (writer != null) {
                try {
                    writer.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    public int countMatching(String prefix, String suffix) {
        File[] files = DiagnosticsDir.get(mCtx).listFiles();
        if (files == null) {
            return 0;
        }
        int count = 0;
        for (File file : files) {
            String name = file.getName();
            if (name.startsWith(prefix) && name.endsWith(suffix)) {
                count++;
            }
        }
        return count;
    }

    public void prune() {
        try {
            File[] files = DiagnosticsDir.get(mCtx).listFiles();
            if (files == null) {
                return;
            }
            long now = System.currentTimeMillis();
            List<File> survivors = new ArrayList<File>();
            long total = 0;
            for (File file : files) {
                if (!file.isFile()) {
                    continue;
                }
                if (now - file.lastModified() > MAX_AGE_MS) {
                    total -= file.length(); // deleted below; keep arithmetic simple
                    file.delete();
                } else {
                    total += file.length();
                    survivors.add(file);
                }
            }
            if (total <= MAX_BYTES) {
                return;
            }
            // Reports last: evict oldest sessions first.
            File[] ordered = survivors.toArray(new File[survivors.size()]);
            Arrays.sort(ordered, new Comparator<File>() {
                @Override
                public int compare(File a, File b) {
                    boolean ra = isReport(a);
                    boolean rb = isReport(b);
                    if (ra != rb) {
                        return ra ? 1 : -1;
                    }
                    return Long.compare(a.lastModified(), b.lastModified());
                }
            });
            for (File file : ordered) {
                if (total <= MAX_BYTES) {
                    return;
                }
                long size = file.length();
                if (file.delete()) {
                    total -= size;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "prune failed", e);
        }
    }

    private static boolean isReport(File file) {
        String name = file.getName();
        return name.startsWith("crash-") || name.startsWith("anr-");
    }

    public String crashFreeSummary() {
        File[] files = DiagnosticsDir.get(mCtx).listFiles();
        if (files == null) {
            return "no diagnostics yet";
        }
        int sessions = 0;
        int crashes = 0;
        int anrs = 0;
        for (File file : files) {
            String name = file.getName();
            if (name.startsWith("session-") && name.endsWith(".jsonl")) {
                sessions++;
            } else if (name.startsWith("crash-") && name.endsWith(".txt")) {
                crashes++;
            } else if (name.startsWith("anr-") && name.endsWith(".txt")) {
                anrs++;
            }
        }
        if (sessions == 0) {
            return "no sessions recorded";
        }
        double free = (1.0d - (crashes / (double) sessions)) * 100.0d;
        return String.format(Locale.US,
                "crash-free sessions: %.1f%% (%d sessions, %d crashes, %d ANRs)",
                free, sessions, crashes, anrs);
    }

    /** Newest first. */
    public File[] allFiles() {
        File[] files = DiagnosticsDir.get(mCtx).listFiles();
        if (files == null) {
            return new File[0];
        }
        Arrays.sort(files, new Comparator<File>() {
            @Override
            public int compare(File a, File b) {
                return Long.compare(b.lastModified(), a.lastModified());
            }
        });
        return files;
    }
}
