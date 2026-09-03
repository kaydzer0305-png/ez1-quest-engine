package com.valvesoftware;

import android.app.ActivityManager;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import com.ezquest.engine.ContentRoots;
import com.ezquest.engine.EngineEnv;
import com.ezquest.engine.GameProfile;
import com.ezquest.engine.Diagnostics;
import com.ezquest.engine.diag.CrashProbe;

/**
 * Quest activity for the EZ1 standalone port.
 *
 * Stays on SDLActivity (flat 2D baseline) but sets up the engine
 * environment using the SourceVR (HL2VR 0.5.1) contract:
 * APK/lib paths, game profile, content roots, locale, plus device
 * diagnostics. The SOURCEVR_* variables are a forward contract for the
 * upcoming OpenXR integration; the nillerusr engine only consumes
 * APP_DATA_PATH / NATIVE_LIB_DIR / -game args today.
 *
 * NOTE: the native methods below are bound BY NAME in
 * launcher/android/main.cpp (Java_com_valvesoftware_ValveActivity2_*).
 * Do NOT rename this class or the native signatures without updating
 * the C++ side to match.
 */
public class ValveActivity2 extends SDLActivity
{
    private static final String TAG = "EZQuest";

    // Native methods provided by liblauncher.so (launcher/android/main.cpp).
    // setenv is currently unused (env goes through EngineEnv/Os.setenv) but
    // kept: the native side binds these BY NAME. setArgs feeds -game args in.
    private static native int setenv(String env, String value, int overwrite);
    private static native void setArgs(String args);

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL2",
            "launcher"
        };
    }

    @Override
    protected String getMainSharedObject() {
        return "liblauncher.so";
    }

    @Override
    protected String getMainFunction() {
        return "LauncherMainAndroid";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            logDeviceInfo();
            CrashProbe.arm(this);
            Diagnostics.startSession(this, "launcher-flat");
            ensureAllFilesAccess();
            setupEngineEnvironment();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "native setup failed (liblauncher.so not loaded?)", e);
        } catch (Exception e) {
            Log.e(TAG, "engine environment setup failed", e);
        }
    }

    @Override
    protected void onDestroy() {
        Diagnostics.endSession(this, true);
        super.onDestroy();
    }

    /**
     * Engine environment via the shared EngineEnv helper (same contract the
     * future EngineActivity uses), mapped onto this project's srceng
     * shared-storage layout.
     */
    private void setupEngineEnvironment() {
        GameProfile profile;
        try {
            profile = GameProfile.forContext(this);
        } catch (Exception e) {
            Log.w(TAG, "no game profile in manifest, defaulting to hl2", e);
            profile = GameProfile.HL2;
        }
        ContentRoots roots = ContentRoots.legacySrceng(this, profile);
        Log.i(TAG, EngineEnv.apply(this, profile, roots));

        // Fail loudly instead of booting into a black screen.
        if (!roots.gameDir.isDirectory()) {
            Log.e(TAG, "game content missing: " + roots.gameDir.getAbsolutePath()
                + " -- push your Steam HL2 files to the " + ContentRoots.CONTENT_ROOT_NAME
                + "/ layout (see README)");
        }
        if (!roots.platformDir.isDirectory()) {
            Log.e(TAG, "platform content missing: " + roots.platformDir.getAbsolutePath());
        }

        setArgs("-game " + roots.gameDir.getAbsolutePath());
    }

    /**
     * targetSdk 34 enforces scoped storage: the manifest entry alone does
     * nothing, the user must grant "All files access" in Settings, or the
     * engine cannot read shared-storage game content.
     */
    private void ensureAllFilesAccess() {
        if (isAllFilesAccessGranted()) {
            return;
        }
        Log.w(TAG, "MANAGE_EXTERNAL_STORAGE not granted; opening system settings");
        try {
            Intent intent = new Intent(
                Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Exception e) {
            Log.w(TAG, "per-app files-access settings unavailable, opening general page", e);
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Exception e2) {
                Log.e(TAG, "could not open files-access settings", e2);
            }
        }
    }

    private static boolean isAllFilesAccessGranted() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    /** RAM / build fingerprint for later perf tuning (cf. SourceVR probes). */
    private void logDeviceInfo() {
        try {
            ActivityManager am = (ActivityManager) getSystemService(ACTIVITY_SERVICE);
            ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
            am.getMemoryInfo(mi);
            Log.i(TAG, "device: model=" + Build.MODEL + " device=" + Build.DEVICE
                + " manufacturer=" + Build.MANUFACTURER + " sdk=" + Build.VERSION.SDK_INT
                + " totalMemMB=" + (mi.totalMem / 1048576L)
                + " availMemMB=" + (mi.availMem / 1048576L)
                + " lowMemory=" + mi.lowMemory
                + " memoryClassMB=" + am.getMemoryClass()
                + " largeMemoryClassMB=" + am.getLargeMemoryClass());
        } catch (Exception e) {
            Log.w(TAG, "device probe failed", e);
        }
    }

}
