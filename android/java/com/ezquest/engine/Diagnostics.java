package com.ezquest.engine;

import android.content.Context;

import com.ezquest.engine.diag.BuildInfo;
import com.ezquest.engine.diag.DiagExport;
import com.ezquest.engine.diag.DiagnosticsDir;
import com.ezquest.engine.diag.EventLog;

import java.io.File;

/**
 * One-line facade over the diagnostics package: session lifecycle,
 * build banner, export. Both entry points (flat launcher activity and
 * engine activity) go through here so sessions are comparable.
 */
public final class Diagnostics {
    private Diagnostics() {
    }

    public static File dir(Context context) {
        return DiagnosticsDir.get(context);
    }

    /** Begin a session: banner to logcat + session_start event (prunes first). */
    public static void startSession(Context context, String entryPoint) {
        try {
            BuildInfo.logBanner(context);
            EventLog.get(context).sessionStart(entryPoint);
        } catch (Exception ignored) {
        }
    }

    public static void endSession(Context context, boolean cleanShutdown) {
        try {
            EventLog.get(context).sessionEnd(cleanShutdown);
        } catch (Exception ignored) {
        }
    }

    /** Human-readable export status (safe to Toast; no string resources exist). */
    public static String exportStatusMessage(Context context) {
        try {
            return DiagExport.exportToDownloads(context);
        } catch (Exception e) {
            return "Export failed: " + e.getMessage();
        }
    }
}
