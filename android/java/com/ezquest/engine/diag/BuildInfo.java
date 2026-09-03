package com.ezquest.engine.diag;

import android.content.Context;
import android.content.pm.PackageInfo;
import android.os.Build;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.Properties;

/**
 * Build + device identity stamped into every report.
 *
 * Build identity comes from the optional {@code assets/ezquest-build.properties}
 * file (keys {@code build_hash}, {@code upstream_pin}; CI can generate it),
 * falling back to the package version. Device fields come from
 * {@link android.os.Build} plus the Quest OS version via getprop.
 */
public final class BuildInfo {
    private static final String ASSET = "ezquest-build.properties";
    private static final String TAG = "EZQuest-Diag";

    private static boolean sLoaded;
    private static String sVersionName = "unknown";
    private static int sVersionCode;
    private static String sBuildHash = "dev";
    private static String sUpstreamPin = "unknown";

    private BuildInfo() {
    }

    private static synchronized void ensure(Context context) {
        if (sLoaded) {
            return;
        }
        sLoaded = true;
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(
                    context.getPackageName(), 0);
            if (info.versionName != null) {
                sVersionName = info.versionName;
            }
            sVersionCode = info.versionCode;
        } catch (Exception e) {
            Log.w(TAG, "package info unavailable", e);
        }
        InputStream in = null;
        try {
            in = context.getAssets().open(ASSET);
            Properties props = new Properties();
            props.load(in);
            String hash = props.getProperty("build_hash");
            if (hash != null && !hash.trim().isEmpty()) {
                sBuildHash = hash.trim();
            }
            String pin = props.getProperty("upstream_pin");
            if (pin != null && !pin.trim().isEmpty()) {
                sUpstreamPin = pin.trim();
            }
        } catch (Exception ignored) {
            // Optional asset; "dev" + package version is a fine stamp.
        } finally {
            if (in != null) {
                try {
                    in.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    public static String versionName(Context context) {
        ensure(context);
        return sVersionName;
    }

    public static String buildHash(Context context) {
        ensure(context);
        return sBuildHash;
    }

    public static String versionLine(Context context) {
        ensure(context);
        return "v" + sVersionName + " (" + sVersionCode + ") build " + sBuildHash;
    }

    public static void logBanner(Context context) {
        ensure(context);
        Log.i(TAG, "Source Quest " + versionLine(context)
                + " upstream=" + sUpstreamPin
                + " pkg=" + context.getPackageName()
                + " device=" + Build.MODEL + "/" + Build.DEVICE
                + " os=" + Build.VERSION.RELEASE
                + " abi=" + firstAbi());
    }

    /** Flat key/value pairs for report headers. */
    public static String[] reportFields(Context context) {
        ensure(context);
        return new String[]{
            "app_version", sVersionName,
            "app_version_code", String.valueOf(sVersionCode),
            "build_hash", sBuildHash,
            "upstream_pin", sUpstreamPin,
            "package", context.getPackageName(),
            "headset_model", Build.MODEL,
            "headset_device", Build.DEVICE,
            "os_release", Build.VERSION.RELEASE,
            "ro_vros_build_version", systemProperty("ro.vros.build.version"),
            "abi", firstAbi(),
        };
    }

    private static String firstAbi() {
        try {
            if (Build.SUPPORTED_ABIS != null && Build.SUPPORTED_ABIS.length > 0) {
                return Build.SUPPORTED_ABIS[0];
            }
        } catch (Exception ignored) {
        }
        return "?";
    }

    public static String systemProperty(String name) {
        BufferedReader reader = null;
        try {
            Process proc = new ProcessBuilder("/system/bin/getprop", name)
                    .redirectErrorStream(true).start();
            reader = new BufferedReader(new InputStreamReader(proc.getInputStream()));
            String line = reader.readLine();
            return line == null ? "" : line.trim();
        } catch (Exception ignored) {
            return "";
        } finally {
            if (reader != null) {
                try {
                    reader.close();
                } catch (Exception ignored) {
                }
            }
        }
    }
}
