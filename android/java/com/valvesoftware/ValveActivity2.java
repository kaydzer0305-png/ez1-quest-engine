package com.valvesoftware;

import org.libsdl.app.SDLActivity;
import android.os.Bundle;
import android.util.Log;

public class ValveActivity2 extends SDLActivity
{
    private static final String TAG = "ValveActivity2";

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
            String filesDir = getExternalFilesDir(null).getAbsolutePath();
            String libDir = getApplicationInfo().nativeLibraryDir;
            setenv("APP_DATA_PATH", filesDir, 1);
            setenv("NATIVE_LIB_DIR", libDir, 1);
            setArgs("-game " + filesDir + "/hl2");
            Log.i(TAG, "env set: APP_DATA_PATH=" + filesDir + " NATIVE_LIB_DIR=" + libDir);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "native setup failed", e);
        }
    }
}
