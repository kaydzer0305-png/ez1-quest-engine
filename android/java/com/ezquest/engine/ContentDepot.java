package com.ezquest.engine;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * One mountable content depot (a device directory with game files).
 *
 * A depot is identified by {@link #dirName} on device; {@link #importDirName}
 * is the folder name users stage/drop (differs when the retail layout names
 * a folder differently from where the engine mounts it).
 */
public final class ContentDepot {
    public static final ContentDepot HL2;
    public static final ContentDepot PLATFORM;
    public static final ContentDepot EPISODIC;
    public static final ContentDepot EP2;
    public static final ContentDepot EZ1;

    private static final Map<String, ContentDepot> BY_DIR;

    static {
        ContentDepot hl2 = new ContentDepot("hl2", "hl2", "hl2", "Half-Life 2");
        HL2 = hl2;
        ContentDepot platform = new ContentDepot("platform", "platform", "platform",
                "Source platform");
        PLATFORM = platform;
        ContentDepot episodic = new ContentDepot("episodic", "episodic", "episodic",
                "Half-Life 2: Episode One");
        EPISODIC = episodic;
        ContentDepot ep2 = new ContentDepot("ep2", "ep2", "ep2",
                "Half-Life 2: Episode Two");
        EP2 = ep2;
        ContentDepot ez1 = new ContentDepot("ez1", "ez1", "ez1", "Entropy: Zero 1");
        EZ1 = ez1;
        Map<String, ContentDepot> byDir = new LinkedHashMap<String, ContentDepot>();
        for (ContentDepot depot : new ContentDepot[]{hl2, platform, episodic, ep2, ez1}) {
            byDir.put(depot.dirName, depot);
        }
        BY_DIR = Collections.unmodifiableMap(byDir);
    }

    public final String id;
    public final String dirName;
    public final String importDirName;
    public final String displayName;

    private ContentDepot(String id, String dirName, String importDirName, String displayName) {
        this.id = id;
        this.dirName = dirName;
        this.importDirName = importDirName;
        this.displayName = displayName;
    }

    public static ContentDepot forDir(String dirName) {
        if (dirName == null) {
            return null;
        }
        return BY_DIR.get(dirName);
    }

    @Override
    public String toString() {
        return dirName;
    }
}
