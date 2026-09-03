package com.ezquest.engine;

import android.content.Context;
import android.os.Build;
import android.util.Log;

import java.io.File;
import java.util.Locale;

/**
 * Sets up the engine process environment before native code loads.
 *
 * Contract (env vars consumed by current + future native code):
 * <ul>
 *   <li>APP_DATA_PATH, NATIVE_LIB_DIR — consumed by launcher/android/main.cpp today</li>
 *   <li>APP_LIB_PATH, SOURCEVR_APK_PATH — apk/lib locations for asset staging</li>
 *   <li>SOURCEVR_GAME — active profile id / game dir name</li>
 *   <li>VALVE_GAME_PATH — user content root the engine mounts</li>
 *   <li>SOURCEVR_WRITE_GAME_PATH — writable per-profile dir (saves, config)</li>
 *   <li>SOURCEVR_SHARED_CONTENT_PATH — shared depot root (== content root until slice C)</li>
 *   <li>EXTRAS_VPK_PATH, VR_CONTENT_PATH — comma-joined overlay dirs (empty until the importer lands)</li>
 *   <li>SOURCEVR_RUNTIME_GRAPH — native .so graph manifest path (empty until slice D)</li>
 *   <li>SOURCEVR_USER_LAUNCH_ARGS_PATH — optional per-profile extra-args file</li>
 *   <li>LANG — ICU locale for the engine (xx_YY, xx_419, ...)</li>
 * </ul>
 * Uses android.system.Os so this can run before any .so is loaded
 * (the engine activity gates run ahead of NativeActivity's library load).
 */
public final class EngineEnv {
    private static final String TAG = "EZQuest-Env";

    /** Optional extra engine args, one profile per file, read verbatim by native code. */
    private static final String LAUNCH_ARGS_DIR = "launch-args";

    private EngineEnv() {
    }

    /**
     * Apply the full environment for {@code profile}/{@code roots}.
     * @return one-line summary for logging.
     */
    public static String apply(Context context, GameProfile profile, ContentRoots roots) {
        String libDir = context.getApplicationInfo().nativeLibraryDir;
        String apkPath = context.getApplicationInfo().sourceDir;

        // Consumed by the engine today.
        setenv("APP_DATA_PATH", roots.filesRoot.getAbsolutePath());
        setenv("NATIVE_LIB_DIR", libDir);

        // APK / library locations.
        setenv("APP_LIB_PATH", libDir);
        setenv("SOURCEVR_APK_PATH", apkPath);

        // Game identity + content mounts.
        setenv("SOURCEVR_GAME", profile.gameDir);
        setenv("VALVE_GAME_PATH", roots.contentRoot.getAbsolutePath());
        setenv("SOURCEVR_WRITE_GAME_PATH", roots.writeGameDir.getAbsolutePath());
        setenv("SOURCEVR_SHARED_CONTENT_PATH", roots.contentRoot.getAbsolutePath());

        // Overlay / graph slots: empty until the importer (slice C) and the
        // native graph manifest (slice D) exist. Empty == absent to the engine.
        setenv("EXTRAS_VPK_PATH", "");
        setenv("VR_CONTENT_PATH", "");
        setenv("SOURCEVR_RUNTIME_GRAPH", "");

        // Optional per-profile extra args file; unset when absent so a stale
        // path from a previous install can never leak into this boot.
        File argsFile = launchArgsFile(roots, profile);
        if (argsFile != null) {
            setenv("SOURCEVR_USER_LAUNCH_ARGS_PATH", argsFile.getAbsolutePath());
        } else {
            unsetenv("SOURCEVR_USER_LAUNCH_ARGS_PATH");
        }

        String lang = icuLangForDefaultLocale();
        if (lang != null) {
            setenv("LANG", lang);
        }

        return "profile=" + profile.id
                + " game=" + roots.gameDir.getAbsolutePath()
                + " writeGame=" + roots.writeGameDir.getAbsolutePath()
                + " launchArgs=" + (argsFile != null ? argsFile.getAbsolutePath() : "(none)")
                + " LANG=" + lang;
    }

    /** {@code <filesRoot>/launch-args/<profile>.txt}, or null when absent. */
    public static File launchArgsFile(ContentRoots roots, GameProfile profile) {
        File file = new File(new File(roots.filesRoot, LAUNCH_ARGS_DIR),
                profile.id + ".txt");
        return file.isFile() ? file : null;
    }

    public static void setenv(String name, String value) {
        try {
            android.system.Os.setenv(name, value, true);
        } catch (Throwable t) {
            Log.e(TAG, "setenv(" + name + ") failed", t);
        }
    }

    public static void unsetenv(String name) {
        try {
            android.system.Os.unsetenv(name);
        } catch (Throwable t) {
            Log.e(TAG, "unsetenv(" + name + ") failed", t);
        }
    }

    /** Device RAM / build fingerprint for later perf tuning. */
    public static String deviceInfoLine() {
        return "model=" + Build.MODEL + " device=" + Build.DEVICE
                + " manufacturer=" + Build.MANUFACTURER
                + " sdk=" + Build.VERSION.SDK_INT;
    }

    /** Best-effort ICU locale for the engine; en_US fallback, null when unknown. */
    public static String icuLangForDefaultLocale() {
        Locale locale = Locale.getDefault();
        if (locale == null || locale.getLanguage() == null
                || locale.getLanguage().isEmpty()) {
            return null;
        }
        String supported = supportedIcuLang(locale);
        return supported != null ? supported : "en_US";
    }

    private static String supportedIcuLang(Locale locale) {
        String language = locale.getLanguage().toLowerCase(Locale.US);
        String country = locale.getCountry() == null
                ? "" : locale.getCountry().toUpperCase(Locale.US);
        if (language.equals("zh")) {
            String script = "";
            try {
                script = locale.getScript();
            } catch (Throwable ignored) {
            }
            if ("Hant".equalsIgnoreCase(script)
                    || "TW".equals(country) || "HK".equals(country) || "MO".equals(country)) {
                return "zh_TW";
            }
            return "zh_CN";
        }
        if (language.equals("de")) return "de_DE";
        if (language.equals("fr")) return "fr_FR";
        if (language.equals("es")) return country.isEmpty() || country.equals("ES") ? "es_ES" : "es_419";
        if (language.equals("it")) return "it_IT";
        if (language.equals("pt")) return "BR".equals(country) ? "pt_BR" : "pt_PT";
        if (language.equals("ru")) return "ru_RU";
        if (language.equals("pl")) return "pl_PL";
        if (language.equals("nl")) return "nl_NL";
        if (language.equals("tr")) return "tr_TR";
        if (language.equals("uk")) return "uk_UA";
        if (language.equals("ja")) return "ja_JP";
        if (language.equals("ko")) return "ko_KR";
        if (language.equals("da")) return "da_DK";
        if (language.equals("fi")) return "fi_FI";
        if (language.equals("sv")) return "sv_SE";
        if (language.equals("nb") || language.equals("nn") || language.equals("no")) return "no_NO";
        if (language.equals("cs")) return "cs_CZ";
        if (language.equals("hu")) return "hu_HU";
        if (language.equals("ro")) return "ro_RO";
        if (language.equals("bg")) return "bg_BG";
        if (language.equals("el")) return "el_GR";
        if (language.equals("th")) return "th_TH";
        if (language.equals("vi")) return "vi_VN";
        if (language.equals("en")) return "en_US";
        return null;
    }
}
