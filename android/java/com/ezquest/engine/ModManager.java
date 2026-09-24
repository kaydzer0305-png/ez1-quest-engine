package com.ezquest.engine;

import android.util.Log;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * {@code custom/} mod management, ported from SourceVR 0.1.25
 * ({@code com.sourcevrport.hl2vr.ModManager} +
 * {@code com.sourcevrport.userdata.CustomContentName}).
 *
 * <p>On-device layout (per game depot, e.g. {@code /sdcard/srceng/ez1/custom/}):
 * <pre>
 *   custom/&lt;mod&gt;/...                     enabled mod
 *   custom/.sourcevr-disabled-&lt;mod&gt;/...   disabled mod (rename, not delete)
 * </pre>
 * Marker names are byte-for-byte SourceVR-compatible, so a mod disabled in
 * SourceVR stays disabled here and vice versa. A legacy
 * {@code <mod>/.sourcevr-disabled} marker <em>file</em> is also honored as
 * disabled (read-only; use the rename form when toggling).
 *
 * <p>Mountable mod content (mirrors SourceVR's
 * {@code UNMOUNTABLE_CONTENT_DIRECTORIES} set — the loose directories the
 * engine mounts from a mod): {@code materials maps models resource scripts
 * sound}, plus {@code *_dir.vpk} / numbered {@code *_NNN.vpk} packs.
 */
public final class ModManager {
    private static final String TAG = "EZQuest-Mods";

    public static final String CUSTOM_DIR_NAME = "custom";
    public static final String DISABLED_MARKER = ".sourcevr-disabled";
    public static final String DISABLED_PREFIX = ".sourcevr-disabled-";

    /** e.g. {@code mypack_dir.vpk} stem + numbered {@code mypack_000.vpk} parts. */
    public static final String DIRECTORY_VPK_SUFFIX = "_dir.vpk";
    private static final Pattern NUMBERED_VPK =
            Pattern.compile("^(.+)_([0-9]+)\\.vpk$", Pattern.CASE_INSENSITIVE);

    public static final Set<String> MOUNTABLE_CONTENT_DIRS =
            Collections.unmodifiableSet(new HashSet<String>(Arrays.asList(
                    "materials", "maps", "models", "resource", "scripts", "sound")));

    public static final class ModEntry {
        /** Canonical name, e.g. {@code my_mod}. */
        public final String name;
        /** On-disk name, e.g. {@code my_mod} or {@code .sourcevr-disabled-my_mod}. */
        public final String physicalName;
        public final boolean enabled;
        public final File dir;

        ModEntry(String name, String physicalName, boolean enabled, File dir) {
            this.name = name;
            this.physicalName = physicalName;
            this.enabled = enabled;
            this.dir = dir;
        }

        @Override
        public String toString() {
            return (enabled ? "[+]" : "[-]") + name;
        }
    }

    private ModManager() {
    }

    /**
     * Parse an on-disk {@code custom/} child name.
     * @return canonical name, or null when the entry is not a mod
     *         (dotfiles other than the disabled prefix, empty, nested dots).
     */
    public static String parseCanonicalName(String physical) {
        Parsed parsed = parse(physical);
        return parsed == null ? null : parsed.canonicalName;
    }

    private static final class Parsed {
        final String canonicalName;
        final boolean enabled;

        Parsed(String canonicalName, boolean enabled) {
            this.canonicalName = canonicalName;
            this.enabled = enabled;
        }
    }

    private static Parsed parse(String physical) {
        if (physical == null || physical.isEmpty()) {
            return null;
        }
        String canonical;
        boolean enabled;
        if (!physical.startsWith(".")) {
            canonical = physical;
            enabled = true;
        } else if (physical.startsWith(DISABLED_PREFIX)) {
            canonical = physical.substring(DISABLED_PREFIX.length());
            enabled = false;
        } else {
            return null;
        }
        if (canonical.isEmpty() || canonical.startsWith(".")) {
            return null;
        }
        if (!isValidModName(canonical)) {
            return null;
        }
        return new Parsed(canonical, enabled);
    }

    /** On-disk name for {@code canonicalName} in the given enabled state. */
    public static String physicalName(String canonicalName, boolean enabled) {
        if (!isValidModName(canonicalName)) {
            throw new IllegalArgumentException("invalid custom-content name");
        }
        return enabled ? canonicalName : DISABLED_PREFIX + canonicalName;
    }

    /**
     * Mod names are path segments, not paths: 1..64 chars,
     * {@code [A-Za-z0-9_.-]}, no {@code ..}, no leading dot
     * (leading dot is the disabled namespace).
     */
    public static boolean isValidModName(String name) {
        if (name == null || name.isEmpty() || name.length() > 64) {
            return false;
        }
        if (name.startsWith(".")) {
            return false;
        }
        if (name.contains("..") || name.contains("/") || name.contains("\\")) {
            return false;
        }
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z')
                    || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9')
                    || c == '_' || c == '-' || c == '.';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    /** {@code <gameDir>/custom}, or null when gameDir is null. */
    public static File customRoot(File gameDir) {
        if (gameDir == null) {
            return null;
        }
        return new File(gameDir, CUSTOM_DIR_NAME);
    }

    /**
     * List mods in {@code <gameDir>/custom}. Empty list when the custom dir
     * is absent (mods are optional). Never returns null.
     */
    public static List<ModEntry> list(File gameDir) {
        List<ModEntry> out = new ArrayList<ModEntry>();
        File root = customRoot(gameDir);
        if (root == null || !root.isDirectory()) {
            return out;
        }
        File[] children = root.listFiles();
        if (children == null) {
            Log.w(TAG, "custom path is not a directory: " + root.getAbsolutePath());
            return out;
        }
        for (File child : children) {
            if (!child.isDirectory()) {
                continue;
            }
            Parsed parsed = parse(child.getName());
            if (parsed == null) {
                continue;
            }
            boolean enabled = parsed.enabled
                    && !new File(child, DISABLED_MARKER).isFile();
            out.add(new ModEntry(parsed.canonicalName, child.getName(),
                    enabled, child));
        }
        return out;
    }

    /** Enabled mod directories only. Never null. */
    public static List<File> enabledModDirs(File gameDir) {
        List<File> out = new ArrayList<File>();
        for (ModEntry entry : list(gameDir)) {
            if (entry.enabled) {
                out.add(entry.dir);
            }
        }
        return out;
    }

    /**
     * Comma-joined enabled mod paths for {@code EXTRAS_VPK_PATH}.
     * Returns "" when there are no enabled mods (empty == absent to native).
     */
    public static String extrasPaths(File gameDir) {
        List<File> enabled = enabledModDirs(gameDir);
        if (enabled.isEmpty()) {
            return "";
        }
        StringBuilder sb = new StringBuilder();
        for (File dir : enabled) {
            if (sb.length() > 0) {
                sb.append(',');
            }
            sb.append(dir.getAbsolutePath());
        }
        return sb.toString();
    }

    /**
     * Enable/disable a mod via atomic rename. Returns true on success.
     * Refuses to overwrite an existing target (e.g. both enabled and
     * disabled copies exist — resolve manually first).
     */
    public static boolean setEnabled(File gameDir, String canonicalName, boolean enabled) {
        if (!isValidModName(canonicalName)) {
            Log.e(TAG, "invalid mod name " + canonicalName);
            return false;
        }
        File root = customRoot(gameDir);
        if (root == null || !root.isDirectory()) {
            Log.e(TAG, "custom directory is unavailable: "
                    + (root == null ? "(null)" : root.getAbsolutePath()));
            return false;
        }
        File src = new File(root, physicalName(canonicalName, !enabled));
        File dst = new File(root, physicalName(canonicalName, enabled));
        // Also accept the legacy in-mod marker file as the disabled source.
        if (!src.isDirectory() && !enabled) {
            File legacyEnabled = new File(root, canonicalName);
            if (legacyEnabled.isDirectory()
                    && new File(legacyEnabled, DISABLED_MARKER).isFile()) {
                src = legacyEnabled;
            }
        }
        if (!src.isDirectory()) {
            Log.e(TAG, "mod entry is unavailable: " + canonicalName);
            return false;
        }
        if (dst.exists()) {
            Log.e(TAG, "both enabled and disabled copies exist for "
                    + canonicalName);
            return false;
        }
        // Safety: never move outside customRoot (canonical names cannot
        // contain separators, but verify the resolved paths anyway).
        try {
            String rootPath = root.getCanonicalPath();
            String srcPath = src.getCanonicalPath();
            if (!srcPath.startsWith(rootPath + File.separator)) {
                Log.e(TAG, "refusing to move a mod outside " + rootPath);
                return false;
            }
        } catch (Exception e) {
            Log.e(TAG, "could not inspect " + canonicalName, e);
            return false;
        }
        if (!src.renameTo(dst)) {
            Log.e(TAG, "could not move " + src.getAbsolutePath()
                    + " to " + dst.getAbsolutePath());
            return false;
        }
        // Drop a legacy marker file when disabling via the enabled dir is
        // impossible (both names collide); normally the rename is enough.
        return true;
    }

    /** True when the mod holds anything the engine mounts (loose dirs or VPKs). */
    public static boolean looksMountable(File modDir) {
        if (modDir == null || !modDir.isDirectory()) {
            return false;
        }
        File[] children = modDir.listFiles();
        if (children == null) {
            return false;
        }
        for (File child : children) {
            String name = child.getName().toLowerCase(java.util.Locale.US);
            if (child.isDirectory() && MOUNTABLE_CONTENT_DIRS.contains(name)) {
                return true;
            }
            if (child.isFile()
                    && (name.endsWith(DIRECTORY_VPK_SUFFIX)
                        || NUMBERED_VPK.matcher(child.getName()).matches()
                        || name.endsWith(".vpk"))) {
                return true;
            }
        }
        return false;
    }
}
