package com.ezquest.engine;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.os.Bundle;

import java.util.Arrays;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * Game content profile: which game this build boots and which content
 * depots (device directories) that game needs.
 *
 * The profile id comes from the {@code com.ezquest.engine.GAME_PROFILE}
 * application meta-data in AndroidManifest.xml. Keep this in sync with
 * the manifest: the engine boots {@link #gameDir}, the (future) importer
 * stages {@link #depots}.
 */
public final class GameProfile {
    /** Manifest meta-data key carrying the profile id. */
    public static final String META_GAME_PROFILE = "com.ezquest.engine.GAME_PROFILE";

    public static final GameProfile HL2;
    public static final GameProfile EZ1;

    private static final Map<String, GameProfile> BY_ID;

    static {
        GameProfile hl2 = new GameProfile("hl2", "Half-Life 2",
                Arrays.asList(ContentDepot.HL2, ContentDepot.PLATFORM));
        HL2 = hl2;
        // Entropy: Zero 1 is an SDK-2013-based mod: its own depot plus the
        // Half-Life 2 / Episode depot chain it mounts content from.
        GameProfile ez1 = new GameProfile("ez1", "Entropy: Zero 1",
                Arrays.asList(ContentDepot.EZ1, ContentDepot.EP2,
                        ContentDepot.EPISODIC, ContentDepot.HL2,
                        ContentDepot.PLATFORM));
        EZ1 = ez1;
        Map<String, GameProfile> byId = new LinkedHashMap<String, GameProfile>();
        byId.put(hl2.id, hl2);
        byId.put(ez1.id, ez1);
        BY_ID = Collections.unmodifiableMap(byId);
    }

    /** Profile id, e.g. "hl2" or "ez1". Doubles as the -game directory name. */
    public final String id;
    public final String displayName;
    /** Primary game directory (first depot). */
    public final String gameDir;
    /** All depots this profile mounts, primary first. */
    public final List<ContentDepot> depots;
    /** The depot the engine boots (depots.get(0)). */
    public final ContentDepot primary;

    private GameProfile(String id, String displayName, List<ContentDepot> depots) {
        this.id = id;
        this.displayName = displayName;
        this.depots = Collections.unmodifiableList(depots);
        this.primary = depots.get(0);
        this.gameDir = primary.dirName;
    }

    public static GameProfile forId(String id) {
        if (id == null) {
            return null;
        }
        for (Map.Entry<String, GameProfile> entry : BY_ID.entrySet()) {
            if (entry.getKey().equalsIgnoreCase(id)) {
                return entry.getValue();
            }
        }
        return null;
    }

    public static GameProfile require(String id) {
        GameProfile profile = forId(id);
        if (profile == null) {
            throw new IllegalStateException("unknown game profile \"" + id
                    + "\"; expected one of " + BY_ID.keySet());
        }
        return profile;
    }

    /** Resolve the active profile from manifest meta-data. */
    public static GameProfile forContext(Context context) {
        String id = null;
        try {
            ApplicationInfo info = context.getPackageManager().getApplicationInfo(
                    context.getPackageName(),
                    android.content.pm.PackageManager.GET_META_DATA);
            Bundle meta = info == null ? null : info.metaData;
            if (meta != null) {
                id = meta.getString(META_GAME_PROFILE);
            }
        } catch (Exception ignored) {
        }
        if (id == null || id.trim().isEmpty()) {
            throw new IllegalStateException("manifest declares no usable "
                    + META_GAME_PROFILE + " meta-data");
        }
        return require(id.trim());
    }

    @Override
    public String toString() {
        return id;
    }
}
