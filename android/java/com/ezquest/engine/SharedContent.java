package com.ezquest.engine;

import android.os.Environment;
import android.os.StatFs;

import java.io.File;
import java.util.List;
import java.util.Locale;

/**
 * Shared depot layout on shared storage.
 *
 * <pre>
 *   &lt;shared&gt;/common/&lt;depot&gt;   mounted game content (hl2, platform, ez1, ...)
 *   &lt;shared&gt;/user/&lt;game&gt;      writable per-profile dir (saves, config)
 * </pre>
 * {@code <shared>} is the existing {@code srceng} root, so legacy
 * {@code srceng/<game>} installs sit alongside the new layout without
 * clashing until they are migrated. Users stage retail folders in
 * {@code Download/EZQuest-<Game>-Import/<depot>} (USB/MTP or adb) and the
 * importer promotes them into {@code common/}.
 */
public final class SharedContent {
    public static final String COMMON_DIR = "common";
    public static final String USER_DIR = "user";
    public static final String IMPORT_PREFIX = "EZQuest-";
    public static final String IMPORT_SUFFIX = "-Import";
    /** Bytes of headroom required beyond the import payload. */
    public static final long IMPORT_FREE_SPACE_RESERVE = 512L * 1024 * 1024;

    private SharedContent() {
    }

    /** Shared root, e.g. /sdcard/srceng. */
    public static File sharedRoot() {
        return new File(Environment.getExternalStorageDirectory(),
                ContentRoots.CONTENT_ROOT_NAME);
    }

    public static File commonRoot() {
        return new File(sharedRoot(), COMMON_DIR);
    }

    public static File depotDir(ContentDepot depot) {
        return new File(commonRoot(), depot.dirName);
    }

    public static File userRoot() {
        return new File(sharedRoot(), USER_DIR);
    }

    public static File writeGameDir(GameProfile profile) {
        return new File(userRoot(), profile.gameDir);
    }

    public static File saveDir(GameProfile profile) {
        return new File(writeGameDir(profile), "save");
    }

    /** e.g. EZQuest-EZ1-Import. */
    public static String stagingDirName(GameProfile profile) {
        return IMPORT_PREFIX + profile.id.toUpperCase(Locale.US) + IMPORT_SUFFIX;
    }

    /** e.g. /sdcard/Download/EZQuest-EZ1-Import. */
    public static File stagingDir(GameProfile profile) {
        File downloads = Environment.getExternalStoragePublicDirectory(
                Environment.DIRECTORY_DOWNLOADS);
        return new File(downloads, stagingDirName(profile));
    }

    public static String stagingAdbPath(GameProfile profile) {
        return "/sdcard/Download/" + stagingDirName(profile);
    }

    /** e.g. /Download/EZQuest-EZ1-Import/{ez1,ep2,episodic,hl2,platform}. */
    public static String stagingMtpPath(GameProfile profile) {
        StringBuilder depots = new StringBuilder();
        for (ContentDepot depot : profile.depots) {
            if (depots.length() > 0) {
                depots.append(',');
            }
            depots.append(depot.importDirName);
        }
        String list = depots.toString();
        if (profile.depots.size() > 1) {
            list = "{" + list + "}";
        }
        return "/Download/" + stagingDirName(profile) + "/" + list;
    }

    /** Copy-paste adb recipe for the user. */
    public static String adbPushInstruction(GameProfile profile) {
        String staging = stagingAdbPath(profile);
        StringBuilder dirs = new StringBuilder();
        StringBuilder dests = new StringBuilder();
        List<ContentDepot> depots = profile.depots;
        for (int i = 0; i < depots.size(); i++) {
            if (i > 0) {
                dirs.append(' ');
                dests.append(' ');
            }
            dirs.append(depots.get(i).importDirName);
            dests.append(staging).append('/').append(depots.get(i).importDirName);
        }
        return "adb shell mkdir -p " + staging
                + " && adb push " + dirs + " " + staging + "/"
                + " && adb shell chmod -R a+rX " + dests;
    }

    /** Usable bytes on the shared-storage volume, -1 when unknown. */
    public static long freeBytes() {
        try {
            return new StatFs(sharedRoot().getAbsolutePath()).getAvailableBytes();
        } catch (Exception ignored) {
            return -1L;
        }
    }

    public static String humanBytes(long bytes) {
        if (bytes < 0) {
            return "unknown";
        }
        if (bytes < 1024) {
            return bytes + " B";
        }
        if (bytes < 1048576) {
            return String.format(Locale.US, "%.1f KB", bytes / 1024.0d);
        }
        if (bytes < 1073741824) {
            return String.format(Locale.US, "%.1f MB", bytes / 1048576.0d);
        }
        return String.format(Locale.US, "%.2f GB", bytes / 1073741824.0d);
    }
}
