package com.ezquest.engine;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.lang.reflect.Method;
import java.util.Arrays;
import java.util.List;

/**
 * Developer profile override, ported from SourceVR 0.1.25
 * ({@code com.sourcevrport.hl2vr.DeveloperProfileLaunch}).
 *
 * Lets a developer boot a profile other than the manifest's
 * {@code com.ezquest.engine.GAME_PROFILE} without editing the manifest
 * or rebuilding — the EZ1 workflow needs exactly this, since CI and the
 * manifest stay on {@code hl2} until {@code /sdcard/srceng/ez1} exists
 * on the headset (see {@code docs/ez1-merge.md}).
 *
 * <p>Gate (all three required, mirroring SourceVR):
 * <ol>
 *   <li>launch intent action == {@link #ACTION}</li>
 *   <li>system property {@link #ENABLE_PROPERTY} == "1"
 *       ({@code adb shell setprop debug.ezquest.dev_payload 1})</li>
 *   <li>intent extra {@link #EXTRA_PROFILE} names a known profile id
 *       (currently {@code hl2} or {@code ez1})</li>
 * </ol>
 *
 * <p>Usage:
 * <pre>
 * adb shell setprop debug.ezquest.dev_payload 1
 * am start -a com.ezquest.engine.action.RUN_DEV_PROFILE \
 *   --es com.ezquest.engine.extra.DEV_GAME_PROFILE ez1 \
 *   -n com.ezquest.engine/.LauncherActivity
 * </pre>
 */
public final class DeveloperProfileLaunch {
    private static final String TAG = "EZQuest-DevProfile";

    public static final String ACTION =
            "com.ezquest.engine.action.RUN_DEV_PROFILE";
    public static final String ENABLE_PROPERTY =
            "debug.ezquest.dev_payload";
    public static final String EXTRA_PROFILE =
            "com.ezquest.engine.extra.DEV_GAME_PROFILE";

    private DeveloperProfileLaunch() {
    }

    /**
     * SourceVR-parity core: return {@code requested} only when the action
     * matches, the enable property is "1", and {@code requested} is in
     * {@code allowedIds}. Otherwise null.
     */
    public static String authorizedProfile(String action, String propValue,
                                           String requested, List<String> allowedIds) {
        if (!ACTION.equals(action)) {
            return null;
        }
        if (!"1".equals(propValue)) {
            return null;
        }
        if (requested == null || allowedIds == null) {
            return null;
        }
        for (String allowed : allowedIds) {
            if (requested.equals(allowed)) {
                return requested;
            }
        }
        return null;
    }

    /** Read a system property via {@code android.os.SystemProperties} reflection. */
    public static String systemProperty(String name) {
        try {
            Class<?> cls = Class.forName("android.os.SystemProperties");
            Method get = cls.getMethod("get", String.class);
            Object value = get.invoke(null, name);
            return value == null ? null : value.toString();
        } catch (Throwable t) {
            Log.w(TAG, "SystemProperties.get(" + name + ") unavailable", t);
            return null;
        }
    }

    /** Allowed dev-override ids: every known GameProfile id. */
    public static List<String> allowedIds() {
        return Arrays.asList(GameProfile.HL2.id, GameProfile.EZ1.id);
    }

    /**
     * Resolve an intent-driven dev override, or null when the gate fails.
     * Never throws: any failure means "no override".
     */
    public static GameProfile overrideFor(Intent intent) {
        try {
            if (intent == null || !ACTION.equals(intent.getAction())) {
                return null;
            }
            Bundle extras = intent.getExtras();
            String requested = extras == null ? null : extras.getString(EXTRA_PROFILE);
            String prop = systemProperty(ENABLE_PROPERTY);
            String authorized = authorizedProfile(
                    intent.getAction(), prop, requested, allowedIds());
            if (authorized == null) {
                Log.w(TAG, "dev override denied (prop=" + prop
                        + " requested=" + requested + ")");
                return null;
            }
            GameProfile profile = GameProfile.forId(authorized);
            if (profile != null) {
                Log.i(TAG, "dev override authorized: " + authorized);
            }
            return profile;
        } catch (Throwable t) {
            Log.w(TAG, "dev override lookup failed", t);
            return null;
        }
    }

    /**
     * Active profile for an activity: dev override wins when authorized,
     * otherwise the manifest profile. Throws only when neither resolves
     * (same contract as {@link GameProfile#forContext}).
     */
    public static GameProfile activeProfile(Context context, Intent intent) {
        GameProfile override = overrideFor(intent);
        if (override != null) {
            return override;
        }
        return GameProfile.forContext(context);
    }

    /**
     * Forward an authorized dev override from one intent to another
     * (launcher -&gt; engine, engine -&gt; flat fallback, router -&gt; importer).
     * No-op when {@code from} carries no authorized override.
     */
    public static void forwardOverride(Intent from, Intent to) {
        try {
            if (from == null || to == null) {
                return;
            }
            GameProfile override = overrideFor(from);
            if (override == null) {
                return;
            }
            to.setAction(ACTION);
            to.putExtra(EXTRA_PROFILE, override.id);
        } catch (Throwable t) {
            Log.w(TAG, "override forward failed", t);
        }
    }
}
