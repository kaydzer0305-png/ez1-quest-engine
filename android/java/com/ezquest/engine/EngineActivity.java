package com.ezquest.engine;

import android.app.NativeActivity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.ezquest.engine.diag.AnrWatchdog;
import com.ezquest.engine.diag.CrashProbe;

/**
 * VR engine activity.
 *
 * Runs in the {@code :engine} process. Before the native library loads it
 * runs the pre-engine gates in order: resolve profile, verify game content,
 * publish the engine environment, then hand over to NativeActivity (which
 * loads {@code android.app.lib_name} and enters native code).
 *
 * <p>The native side (launcher/android/vr_main.cpp, slice D) implements
 * {@code ANativeActivity_onCreate} and boots the OpenXR session/swapchain/
 * compositor loop. This activity polls {@link #nativeXrStatus()} until the
 * loop presents (EZ_XR_RUNNING) or fails/times out, in which case it falls
 * back to the flat SDL engine ({@code ValveActivity2}) so the device keeps
 * a working boot path (BOOT_MODE safe-flip contract).
 */
public class EngineActivity extends NativeActivity {
    private static final String TAG = "EZQuest-Engine";

    /** Explicit action other components use to start the engine. */
    public static final String ACTION_START_ENGINE =
            "com.ezquest.engine.action.START_ENGINE";

    /** Mirror of EzVrStatus_e in launcher/android/vr_main.cpp. */
    private static final int EZ_XR_STARTING = 0;
    private static final int EZ_XR_RUNNING = 1;
    private static final int EZ_XR_FAILED = 2;

    private static final long XR_POLL_MS = 500;
    private static final long XR_TIMEOUT_MS = 20000;

    private final Handler mHandler = new Handler(Looper.getMainLooper());
    private long mXrPolledMs;
    private boolean mFallbackStarted;

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
            beginXrStatusPolling();
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
     * Slice D contract: liblauncher.so's ANativeActivity worker boots the
     * OpenXR loop in the background. Poll its status; on failure (or if the
     * loop never presents within the timeout) fall back to the flat engine
     * so the headset always has a usable boot path.
     */
    private void beginXrStatusPolling() {
        mXrPolledMs = 0;
        mHandler.postDelayed(mXrPollRunnable, XR_POLL_MS);
    }

    private final Runnable mXrPollRunnable = new Runnable() {
        @Override
        public void run() {
            if (mFallbackStarted || isFinishing()) {
                return;
            }
            mXrPolledMs += XR_POLL_MS;

            int status;
            try {
                status = nativeXrStatus();
            } catch (UnsatisfiedLinkError e) {
                Log.e(TAG, "XR status symbols missing from liblauncher.so", e);
                status = EZ_XR_FAILED;
            }

            if (status == EZ_XR_RUNNING) {
                Log.i(TAG, "XR loop is presenting");
                return; // stay resident; native owns presentation from here
            }
            if (status == EZ_XR_FAILED || mXrPolledMs >= XR_TIMEOUT_MS) {
                String reason;
                try {
                    reason = nativeXrFailReason();
                } catch (UnsatisfiedLinkError e) {
                    reason = null;
                }
                Log.e(TAG, "XR bootstrap failed (status=" + status + " polledMs="
                        + mXrPolledMs + " reason=" + reason + "); falling back to flat");
                fallBackToFlat(status == EZ_XR_FAILED ? reason : "XR loop did not present in time");
                return;
            }
            mHandler.postDelayed(this, XR_POLL_MS);
        }
    };

    private void fallBackToFlat(String reason) {
        if (mFallbackStarted) {
            return;
        }
        mFallbackStarted = true;
        mHandler.removeCallbacks(mXrPollRunnable);
        Log.w(TAG, "flat fallback: " + reason);
        try {
            startActivity(new Intent(this, ValveActivity2.class));
        } catch (Exception e) {
            Log.e(TAG, "flat fallback failed", e);
            RecoveryActivity.launch(this, "xr",
                    "XR boot failed (" + reason + ") and the flat engine could not start.");
        }
        finish();
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

    // Bound by name in launcher/android/vr_main.cpp.
    private static native int nativeXrStatus();
    private static native String nativeXrFailReason();
}
