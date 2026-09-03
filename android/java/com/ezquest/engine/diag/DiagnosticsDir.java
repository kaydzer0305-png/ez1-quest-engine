package com.ezquest.engine.diag;

import android.content.Context;

import java.io.File;

/**
 * The {@code diagnostics/} directory (created on demand): session logs,
 * crash/ANR reports, and the optional native frame-loop heartbeat file.
 */
public final class DiagnosticsDir {
    private DiagnosticsDir() {
    }

    public static File get(Context context) {
        File root = context.getExternalFilesDir(null);
        if (root == null) {
            root = context.getFilesDir();
        }
        File dir = new File(root, "diagnostics");
        dir.mkdirs();
        return dir;
    }
}
