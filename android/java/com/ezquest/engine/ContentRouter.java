package com.ezquest.engine;

import android.app.Activity;
import android.content.Intent;
import android.util.Log;

/**
 * Decides whether an activity may proceed to boot: shared depot layout
 * ready, or legacy layout present → proceed; otherwise route to the
 * importer. Returns true when the caller must stop (it launched the
 * importer).
 */
public final class ContentRouter {
    private static final String TAG = "EZQuest-Content";

    private ContentRouter() {
    }

    public static boolean routeIfNeeded(Activity activity) {
        GameProfile profile;
        try {
            profile = GameProfile.forContext(activity);
        } catch (Exception e) {
            Log.w(TAG, "profile unreadable here; engine gate will report it", e);
            return false;
        }
        if (ContentRoots.resolve(activity, profile) != null) {
            return false;
        }
        // Resolve failed: name the exact missing files so the log (and the
        // importer) can tell the user what to stage, e.g.
        // "hl2 is missing: hl2_misc_dir.vpk".
        try {
            ContentRoots legacy = ContentRoots.legacySrceng(activity, profile);
            ContentPresence presence =
                    ContentPresence.check(legacy.contentRoot, profile);
            Log.i(TAG, "content not launch-ready (" + presence.problem()
                    + "); routing to importer");
        } catch (Exception e) {
            Log.i(TAG, "no usable " + profile.displayName
                    + " content; routing to importer (" + e + ")");
        }
        try {
            activity.startActivity(ImportActivity.intentFor(activity));
        } catch (Exception e) {
            Log.e(TAG, "could not open importer", e);
        }
        return true;
    }

    public static void openImporter(Activity activity) {
        activity.startActivity(new Intent(activity, ImportActivity.class));
    }
}
