package com.ezquest.engine;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

/**
 * Readiness check for one profile against a depot root: every depot
 * directory present plus the primary's {@code gameinfo.txt} bootstrap.
 */
public final class ContentPresence {
    public static final String READY = "ready";
    public static final String MISSING = "missing";

    public final GameProfile profile;
    public final File commonRoot;
    public final List<DepotStatus> depots = new ArrayList<DepotStatus>();
    public boolean bootstrapPresent;
    public final List<String> missingRequired = new ArrayList<String>();
    public String verdict = MISSING;
    public String headline = "";

    public static final class DepotStatus {
        public final ContentDepot depot;
        public final File root;
        public final boolean present;

        DepotStatus(ContentDepot depot, File root) {
            this.depot = depot;
            this.root = root;
            this.present = root != null && root.isDirectory();
        }
    }

    private ContentPresence(GameProfile profile, File commonRoot) {
        this.profile = profile;
        this.commonRoot = commonRoot;
    }

    public static ContentPresence check(File commonRoot, GameProfile profile) {
        return check(commonRoot, profile, null);
    }

    public static ContentPresence check(File commonRoot, GameProfile profile, Cancel cancel) {
        if (profile == null) {
            profile = GameProfile.HL2;
        }
        ContentPresence presence = new ContentPresence(profile, commonRoot);
        boolean allPresent = true;
        File primaryRoot = null;
        for (ContentDepot depot : profile.depots) {
            if (cancel != null) {
                cancel.throwIfCancelled();
            }
            File root = commonRoot == null ? null : new File(commonRoot, depot.dirName);
            DepotStatus status = new DepotStatus(depot, root);
            presence.depots.add(status);
            if (!status.present) {
                allPresent = false;
            }
            if (depot == profile.primary) {
                primaryRoot = root;
            }
        }
        presence.bootstrapPresent = primaryRoot != null
                && new File(primaryRoot, "gameinfo.txt").isFile();
        if (primaryRoot != null) {
            for (String required : profile.requiredFiles) {
                if (cancel != null) {
                    cancel.throwIfCancelled();
                }
                if (!new File(primaryRoot, required).isFile()) {
                    presence.missingRequired.add(required);
                }
            }
        } else {
            presence.missingRequired.addAll(profile.requiredFiles);
        }
        if (allPresent && presence.bootstrapPresent
                && presence.missingRequired.isEmpty()) {
            presence.verdict = READY;
            presence.headline = profile.displayName + " folders are ready to launch.";
        } else {
            presence.verdict = MISSING;
            presence.headline = profile.displayName
                    + " needs its retail folders and gameinfo.txt.";
        }
        return presence;
    }

    public boolean readyToLaunch() {
        return READY.equals(verdict);
    }

    public List<DepotStatus> missingDepots() {
        List<DepotStatus> missing = new ArrayList<DepotStatus>();
        for (DepotStatus status : depots) {
            if (!status.present) {
                missing.add(status);
            }
        }
        return missing;
    }

    public String missingDepotNames() {
        StringBuilder sb = new StringBuilder();
        for (DepotStatus status : missingDepots()) {
            if (sb.length() > 0) {
                sb.append(", ");
            }
            sb.append(status.depot.dirName).append('/');
        }
        return sb.toString();
    }

    public String problem() {
        String missing = missingDepotNames();
        if (!missing.isEmpty()) {
            return "missing retail folders: " + missing;
        }
        if (!bootstrapPresent) {
            return profile.gameDir + "/gameinfo.txt is missing";
        }
        if (!missingRequired.isEmpty()) {
            StringBuilder sb = new StringBuilder(profile.gameDir + " is missing: ");
            for (int i = 0; i < missingRequired.size(); i++) {
                if (i > 0) {
                    sb.append(", ");
                }
                sb.append(missingRequired.get(i));
            }
            return sb.toString();
        }
        return "required content folders are unavailable";
    }
}
