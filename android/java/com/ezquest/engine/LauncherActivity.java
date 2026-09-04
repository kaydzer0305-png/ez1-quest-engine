package com.ezquest.engine;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.widget.TextView;

import com.valvesoftware.ValveActivity2;

/**
 * 2D bootstrap entry (MAIN/LAUNCHER). Mirrors SourceVR's LauncherActivity:
 * a plain panel activity with no surface and no VR categories that runs the
 * content/profile gates, then starts the engine activity explicitly.
 *
 * Why this exists: on Quest the system loading interstitial holds window
 * focus until the first frame is presented, but SDL's engine thread only
 * starts after (surface ready + resumed + focused). Booting straight into
 * the SDL activity deadlocks: no focus -> no thread -> no frame -> no
 * focus, forever. A 2D launcher draws immediately (dismissing the
 * interstitial and taking focus), so by the time the engine activity
 * starts, focus flows normally and SDL's state machine proceeds.
 *
 * Flat baseline: routes to ValveActivity2 (SDL). The boot target is chosen
 * by the {@link #META_BOOT_MODE} application meta-data (vr|flat, default
 * flat): vr routes to EngineActivity, whose native side boots the OpenXR
 * loop and falls back to the flat engine on its own if that fails, so this
 * class never has to second-guess the native bootstrap.
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "EZQuest";

    /** Application meta-data selecting the boot path: "vr" or "flat". */
    public static final String META_BOOT_MODE = "com.ezquest.engine.BOOT_MODE";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView status = new TextView(this);
        status.setText("Source Quest: checking content...");
        setContentView(status);

        Diagnostics.startSession(this, "launcher");

        // Content gate: missing layout -> importer (which returns the user
        // here on relaunch); present -> engine.
        if (ContentRouter.routeIfNeeded(this)) {
            Log.i(TAG, "launcher: content missing, handed off to importer");
            finish();
            return;
        }

        String bootMode = bootMode();
        Log.i(TAG, "launcher: content OK, boot mode " + bootMode);
        try {
            if ("vr".equals(bootMode)) {
                startActivity(new Intent(this, EngineActivity.class));
            } else {
                startActivity(new Intent(this, ValveActivity2.class));
            }
        } catch (Exception e) {
            Log.e(TAG, "launcher: could not start engine", e);
        }
        finish();
    }

    /** BOOT_MODE meta-data, defaulting to flat (the verified baseline). */
    private String bootMode() {
        try {
            android.content.pm.PackageManager pm = getPackageManager();
            android.content.pm.ApplicationInfo ai =
                    pm.getApplicationInfo(getPackageName(),
                            android.content.pm.PackageManager.GET_META_DATA);
            if (ai.metaData != null) {
                String mode = ai.metaData.getString(META_BOOT_MODE);
                if (mode != null && !mode.isEmpty()) {
                    return mode;
                }
            }
        } catch (Exception e) {
            Log.w(TAG, "boot mode lookup failed; defaulting to flat", e);
        }
        return "flat";
    }
}
