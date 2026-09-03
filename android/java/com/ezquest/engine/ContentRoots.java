package com.ezquest.engine;

import android.content.Context;
import android.os.Environment;

import java.io.File;

/**
 * On-device content locations for one {@link GameProfile}.
 *
 * {@link #legacySrceng} preserves the layout the flat SDL boot path uses
 * today ({@code <shared-storage>/srceng/<game|platform>}, writable game dir
 * under the app files dir), so existing installs keep working byte for
 * byte while the engine environment is extended around them. The shared
 * depot layout (common depots + per-user write dir) used by the future
 * content importer is layered on in slice C.
 */
public final class ContentRoots {
    /** Name of the shared-storage content root, e.g. /sdcard/srceng. */
    public static final String CONTENT_ROOT_NAME = "srceng";
    public static final String PLATFORM_DIR_NAME = "platform";

    /** User-supplied game content, e.g. /sdcard/srceng. */
    public final File contentRoot;
    /** Game directory, e.g. /sdcard/srceng/hl2. */
    public final File gameDir;
    /** Shared platform directory, e.g. /sdcard/srceng/platform. */
    public final File platformDir;
    /** Writable per-profile dir for saves/config, under the app files dir. */
    public final File writeGameDir;
    /** App files root (getExternalFilesDir, falling back to getFilesDir). */
    public final File filesRoot;
    /** Which layout this is: "shared" (common/ depots) or "legacy" (srceng/ flat). */
    public final String layout;

    private ContentRoots(String layout, File contentRoot, File gameDir, File platformDir,
                         File writeGameDir, File filesRoot) {
        this.layout = layout;
        this.contentRoot = contentRoot;
        this.gameDir = gameDir;
        this.platformDir = platformDir;
        this.writeGameDir = writeGameDir;
        this.filesRoot = filesRoot;
    }

    public static File filesRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        return external != null ? external : context.getFilesDir();
    }

    /** Current device layout: game + platform depots straight off srceng. */
    public static ContentRoots legacySrceng(Context context, GameProfile profile) {
        File contentRoot = new File(Environment.getExternalStorageDirectory(),
                CONTENT_ROOT_NAME);
        File files = filesRoot(context);
        return new ContentRoots("legacy",
                contentRoot,
                new File(contentRoot, profile.gameDir),
                new File(contentRoot, PLATFORM_DIR_NAME),
                new File(new File(files, profile.gameDir), ""),
                files);
    }

    /** Shared depot layout: mounts from common/, writes under user/. */
    public static ContentRoots sharedDepot(Context context, GameProfile profile) {
        File common = SharedContent.commonRoot();
        return new ContentRoots("shared",
                SharedContent.userRoot(),
                new File(common, profile.gameDir),
                new File(common, PLATFORM_DIR_NAME),
                SharedContent.writeGameDir(profile),
                filesRoot(context));
    }

    /**
     * Best usable roots: shared layout when it is launch-ready, else the
     * legacy layout when it has content, else null (caller routes to the
     * importer).
     */
    public static ContentRoots resolve(Context context, GameProfile profile) {
        if (ContentPresence.check(SharedContent.commonRoot(), profile).readyToLaunch()) {
            return sharedDepot(context, profile);
        }
        ContentRoots legacy = legacySrceng(context, profile);
        if (legacy.hasGameContent()) {
            return legacy;
        }
        return null;
    }

    public boolean hasGameContent() {
        return gameDir.isDirectory() && platformDir.isDirectory();
    }

    public String missingWhat() {
        if (!gameDir.isDirectory() && !platformDir.isDirectory()) {
            return gameDir.getAbsolutePath() + " and " + platformDir.getAbsolutePath();
        }
        if (!gameDir.isDirectory()) {
            return gameDir.getAbsolutePath();
        }
        return platformDir.getAbsolutePath();
    }
}
