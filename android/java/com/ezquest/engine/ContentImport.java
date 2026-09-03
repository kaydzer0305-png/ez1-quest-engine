package com.ezquest.engine;

import com.ezquest.engine.diag.EventLog;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Promotes staged retail folders ({@code Download/EZQuest-<Game>-Import/})
 * into the shared {@code common/} depot layout.
 *
 * Transaction per depot: copy to {@code <depot>.part}, move any existing
 * depot aside to {@code <depot>.backup-<epoch>}, rename the part into
 * place, then delete the backup. The live depot is therefore never
 * half-written: a failure or cancel leaves either the old depot or no
 * depot, and the part dir is always removed. Cooperative cancellation
 * between files.
 */
public final class ContentImport {
    /** Single-flight gate so only one import runs per process. */
    private static boolean sRunning;

    public interface Listener {
        void onProgress(long doneBytes, long totalBytes);
        void onDepotDone(ContentDepot depot);
    }

    private ContentImport() {
    }

    public static synchronized boolean tryBegin() {
        if (sRunning) {
            return false;
        }
        sRunning = true;
        return true;
    }

    public static synchronized void end() {
        sRunning = false;
    }

    public static synchronized boolean isRunning() {
        return sRunning;
    }

    /**
     * @return bytes promoted.
     * @throws IOException on I/O failure or unmet preconditions.
     */
    public static long run(Context context, GameProfile profile,
                           File stagingRoot, File commonRoot,
                           Listener listener, Cancel cancel) throws IOException {
        if (stagingRoot == null || !stagingRoot.isDirectory()) {
            throw new IOException("staging folder not found: " + stagingRoot);
        }
        List<DepotCopy> plan = new ArrayList<DepotCopy>();
        long total = 0;
        for (ContentDepot depot : profile.depots) {
            if (cancel != null) {
                cancel.throwIfCancelled();
            }
            File src = new File(stagingRoot, depot.importDirName);
            if (!src.isDirectory()) {
                throw new IOException("staging is missing " + depot.importDirName + "/ — "
                        + "push it to " + stagingRoot.getAbsolutePath() + " first");
            }
            long bytes = measureBytes(src, cancel);
            plan.add(new DepotCopy(depot, src, bytes));
            total += bytes;
        }
        long free = SharedContent.freeBytes();
        if (free >= 0 && total + SharedContent.IMPORT_FREE_SPACE_RESERVE > free) {
            throw new IOException("not enough free space: need "
                    + SharedContent.humanBytes(total) + " + "
                    + SharedContent.humanBytes(SharedContent.IMPORT_FREE_SPACE_RESERVE)
                    + " reserve, have " + SharedContent.humanBytes(free));
        }
        if (!commonRoot.isDirectory() && !commonRoot.mkdirs()) {
            throw new IOException("could not create " + commonRoot.getAbsolutePath());
        }
        long done = 0;
        for (DepotCopy copy : plan) {
            if (cancel != null) {
                cancel.throwIfCancelled();
            }
            promoteDepot(copy, new File(commonRoot, copy.depot.dirName),
                    listener, cancel, new long[]{done, total});
            done += copy.bytes;
            if (listener != null) {
                listener.onDepotDone(copy.depot);
            }
        }
        EventLog.get(context).event("content_import",
                "profile", profile.id,
                "bytes", String.valueOf(done),
                "depots", String.valueOf(plan.size()));
        return done;
    }

    private static void promoteDepot(DepotCopy copy, File dest,
                                     Listener listener, Cancel cancel,
                                     long[] progress) throws IOException {
        File part = new File(dest.getParentFile(), dest.getName() + ".part");
        deleteTree(part);
        copyTree(copy.src, part, listener, cancel, progress);
        File backup = null;
        if (dest.exists()) {
            backup = new File(dest.getParentFile(),
                    dest.getName() + ".backup-" + (System.currentTimeMillis() / 1000));
            if (!dest.renameTo(backup)) {
                deleteTree(part);
                throw new IOException("could not move aside " + dest.getAbsolutePath());
            }
        }
        if (!part.renameTo(dest)) {
            deleteTree(part);
            if (backup != null) {
                backup.renameTo(dest);
            }
            throw new IOException("could not promote " + dest.getAbsolutePath());
        }
        if (backup != null) {
            deleteTree(backup);
        }
    }

    private static long measureBytes(File root, Cancel cancel) throws IOException {
        long total = 0;
        File[] files = root.listFiles();
        if (files == null) {
            throw new IOException("cannot list " + root.getAbsolutePath());
        }
        for (File file : files) {
            if (cancel != null) {
                cancel.throwIfCancelled();
            }
            if (file.isDirectory()) {
                total += measureBytes(file, cancel);
            } else {
                total += file.length();
            }
        }
        return total;
    }

    private static void copyTree(File src, File dest, Listener listener,
                                 Cancel cancel, long[] progress) throws IOException {
        if (src.isDirectory()) {
            if (!dest.isDirectory() && !dest.mkdirs()) {
                throw new IOException("cannot create " + dest.getAbsolutePath());
            }
            File[] files = src.listFiles();
            if (files == null) {
                throw new IOException("cannot list " + src.getAbsolutePath());
            }
            for (File file : files) {
                if (cancel != null) {
                    cancel.throwIfCancelled();
                }
                copyTree(file, new File(dest, file.getName()), listener, cancel, progress);
            }
            return;
        }
        FileInputStream in = new FileInputStream(src);
        try {
            FileOutputStream out = new FileOutputStream(dest);
            try {
                byte[] buf = new byte[65536];
                int read;
                while ((read = in.read(buf)) > 0) {
                    if (cancel != null) {
                        cancel.throwIfCancelled();
                    }
                    out.write(buf, 0, read);
                    progress[0] += read;
                    if (listener != null) {
                        listener.onProgress(progress[0], progress[1]);
                    }
                }
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }

    private static void deleteTree(File root) {
        if (root == null || !root.exists()) {
            return;
        }
        File[] files = root.listFiles();
        if (files != null) {
            for (File file : files) {
                deleteTree(file);
            }
        }
        root.delete();
    }

    private static final class DepotCopy {
        final ContentDepot depot;
        final File src;
        final long bytes;

        DepotCopy(ContentDepot depot, File src, long bytes) {
            this.depot = depot;
            this.src = src;
            this.bytes = bytes;
        }
    }
}
