package com.ezquest.engine;

import android.app.NativeActivity;
import android.os.Bundle;
import android.util.Log;

import com.ezquest.engine.diag.AnrWatchdog;
import com.ezquest.engine.diag.CrashProbe;

/**
 * VR engine activity (future boot path; not the launcher yet).
 *
 * Runs in the {@code :engine} process. Before the native library loads it
 * runs the pre-engine gates in order: resolve profile, verify game content,
 * publish the engine environment, then hand over to NativeActivity (which
 * loads {@code android.app.lib_name} and enters native code).
 *
 * <p>NOT wired to boot yet: it has no MAIN/LAUNCHER intent filter, because
 * the native side does not export ANativeActivity_onCreate (slice D /
 * private OpenXR tree). Launching it before then fails loudly via
 * {@link RecoveryActivity} instead of a black screen. The flat SDL path
 * ({@code ValveActivity2}) remains the working entry point meanwhile.
 */
public class EngineActivity extends NativeActivity {
    private static final String TAG = "EZQuest-Engine";

    /** Explicit action other components use to start the engine. */
    public static final String ACTION_START_ENGINE =
            "com.ezquest.engine.action.START_ENGINE";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        GameProfile profile;
        try {
            profile = GameProfile.forContext(this);
        } catch (Exception e) {
            Log.e(TAG, "no usable game profile", e);
            RecoveryActivity.launch(this, "profile",
                    "This build declares no usable game profile: " + e.getMessage());
            finish();
            return;
        }

        ContentRoots roots = ContentRoots.resolve(this, profile);
        if (ContentRouter.routeIfNeeded(this)) {
            Log.i(TAG, "no usable content; importer routed, engine stopping");
            finish();
            return;
        }
        if (roots == null) {
            // Lost a race with the router (content vanished mid-check).
            RecoveryActivity.launch(this, "content",
                    "Game content disappeared while starting. Open the importer from the launcher.");
            finish();
            return;
        }
        Log.i(TAG, "content layout: " + roots.layout
                + " game=" + roots.gameDir.getAbsolutePath());

        Log.i(TAG, EngineEnv.apply(this, profile, roots));
        onEngineEnvironmentReady(profile, roots);

        try {
            super.onCreate(savedInstanceState);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "native engine library failed to load", e);
            RecoveryActivity.launch(this, "native",
                    "The native engine library could not be loaded: " + e.getMessage());
            finish();
        } catch (Exception e) {
            Log.e(TAG, "engine startup failed", e);
            RecoveryActivity.launch(this, "startup",
                    "Engine startup failed: " + e.getMessage());
            finish();
        }
    }

    /**
     * Slice B wiring: session log, Java crash capture, ANR watchdog, then
     * the device-state bridge. Runs after env setup, before native load.
     */
    protected void onEngineEnvironmentReady(GameProfile profile, ContentRoots roots) {
        Diagnostics.startSession(this, "engine:" + profile.id);
        CrashProbe.arm(this);
        AnrWatchdog.get(this).start();
        DeviceBridge.get(this).start();
    }

    @Override
    protected void onDestroy() {
        onEngineTeardown();
        super.onDestroy();
    }

    /** Slice B wiring: stop bridge + watchdog, close the session. */
    protected void onEngineTeardown() {
        try {
            DeviceBridge.get(this).stop();
        } catch (Exception ignored) {
        }
        try {
            AnrWatchdog.get(this).stop();
        } catch (Exception ignored) {
        }
        Diagnostics.endSession(this, true);
    }
}
