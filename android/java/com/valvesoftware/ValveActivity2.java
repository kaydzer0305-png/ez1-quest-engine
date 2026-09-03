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

import java.io.File;
import java.util.Locale;

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

    // Game content on shared storage (user-supplied, never redistributed).
    private static final String CONTENT_ROOT_NAME = "srceng";
    private static final String GAME_DIR = "hl2"; // TODO(EZ1): "ez1" once the mod game code merges
    private static final String PLATFORM_DIR = "platform";

    // Native methods provided by liblauncher.so (launcher/android/main.cpp)
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
            ensureAllFilesAccess();
            setupEngineEnvironment();
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "native setup failed (liblauncher.so not loaded?)", e);
        } catch (Exception e) {
            Log.e(TAG, "engine environment setup failed", e);
        }
    }

    /**
     * Mirror of SourceVR MainActivity.setupEngineEnvironment, mapped onto
     * this project's srceng shared-storage layout.
     */
    private void setupEngineEnvironment() {
        String filesDir = getExternalFilesDir(null).getAbsolutePath();
        String libDir = getApplicationInfo().nativeLibraryDir;
        String apkPath = getApplicationInfo().sourceDir;

        File contentRoot = new File(Environment.getExternalStorageDirectory(), CONTENT_ROOT_NAME);
        File gameDir = new File(contentRoot, GAME_DIR);
        File platformDir = new File(contentRoot, PLATFORM_DIR);
        File writeGameDir = new File(filesDir, GAME_DIR);

        // Consumed by the engine today (see launcher/android/main.cpp).
        setenv("APP_DATA_PATH", filesDir, 1);
        setenv("NATIVE_LIB_DIR", libDir, 1);

        // SourceVR forward contract (ignored by the engine for now).
        setenv("APP_LIB_PATH", libDir, 1);
        setenv("SOURCEVR_APK_PATH", apkPath, 1);
        setenv("SOURCEVR_GAME", GAME_DIR, 1);
        setenv("VALVE_GAME_PATH", contentRoot.getAbsolutePath(), 1);
        setenv("SOURCEVR_WRITE_GAME_PATH", writeGameDir.getAbsolutePath(), 1);
        String lang = icuLangForDefaultLocale();
        if (lang != null) {
            setenv("LANG", lang, 1);
        }

        // Fail loudly instead of booting into a black screen.
        if (!gameDir.isDirectory()) {
            Log.e(TAG, "game content missing: " + gameDir.getAbsolutePath()
                + " -- push your Steam HL2 files to the " + CONTENT_ROOT_NAME
                + "/ layout (see README)");
        }
        if (!platformDir.isDirectory()) {
            Log.e(TAG, "platform content missing: " + platformDir.getAbsolutePath());
        }

        setArgs("-game " + gameDir.getAbsolutePath());
        Log.i(TAG, "engine env: game=" + GAME_DIR
            + " contentRoot=" + contentRoot.getAbsolutePath()
            + " writeGame=" + writeGameDir.getAbsolutePath()
            + " allFilesAccess=" + isAllFilesAccessGranted()
            + " LANG=" + lang);
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

    /** Minimal ICU locale mapping (cf. SourceVR MainActivity); defaults to en_US. */
    private static String icuLangForDefaultLocale() {
        String language = Locale.getDefault().getLanguage();
        if (language == null || language.isEmpty()) {
            return null;
        }
        switch (language) {
            case "de": return "de_DE";
            case "fr": return "fr_FR";
            case "es": return "es_ES";
            case "it": return "it_IT";
            case "pt": return "pt_BR";
            case "ru": return "ru_RU";
            case "pl": return "pl_PL";
            case "nl": return "nl_NL";
            case "tr": return "tr_TR";
            case "uk": return "uk_UA";
            case "ja": return "ja_JP";
            case "ko": return "ko_KR";
            case "zh": return "zh_CN";
            default: return "en_US";
        }
    }
}
