package com.ezquest.engine;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.util.Log;

/**
 * Pushes device state (battery, thermal) into the engine and reports
 * whether native code is up.
 *
 * The three natives below are a forward contract for slice D: the engine
 * library must export
 * <ul>
 *   <li>{@code Java_com_ezquest_engine_DeviceBridge_nativeEngineUp} —
 *       returns nonzero once the engine frame loop is running</li>
 *   <li>{@code Java_com_ezquest_engine_DeviceBridge_nativePushBattery} —
 *       stores battery percent (-1 = unknown) + charging flag</li>
 *   <li>{@code Java_com_ezquest_engine_DeviceBridge_nativePushThermal} —
 *       stores the PowerManager thermal status (-1 = unknown)</li>
 * </ul>
 * Until those exist every push is caught and treated as "engine not up";
 * sampling + logging keep working regardless.
 */
public final class DeviceBridge {
    private static final long POLL_MS = 30000;
    private static final String TAG = "EZQuest-Bridge";

    private static DeviceBridge sInstance;

    /** Charging statuses from the BATTERY_CHANGED sticky intent. */
    private static final int BATTERY_STATUS_CHARGING = 2;
    private static final int BATTERY_STATUS_FULL = 5;

    private final Context mCtx;
    private PowerManager mPower;
    private boolean mStarted;
    private boolean mSawEngine;
    private boolean mBridgeMissingLogged;
    private final Handler mHandler = new Handler(Looper.getMainLooper());

    private final BroadcastReceiver mPowerEdge = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            sample("power-edge");
        }
    };

    private final Runnable mTick = new Runnable() {
        @Override
        public void run() {
            sample("tick");
            if (mStarted) {
                mHandler.postDelayed(this, POLL_MS);
            }
        }
    };

    private static native boolean nativeEngineUp();

    private static native boolean nativePushBattery(int percent, boolean charging);

    private static native void nativePushThermal(int status);

    static {
        try {
            // Already loaded by the hosting activity; a no-op then. The
            // bridge *symbols* only exist from slice D on — absence is
            // handled per-call below, not here.
            System.loadLibrary("launcher");
        } catch (Throwable t) {
            Log.w(TAG, "engine library not loaded; bridge runs reduced", t);
        }
    }

    private DeviceBridge(Context context) {
        mCtx = context.getApplicationContext();
    }

    public static synchronized DeviceBridge get(Context context) {
        if (sInstance == null) {
            sInstance = new DeviceBridge(context);
        }
        return sInstance;
    }

    public synchronized void start() {
        if (mStarted) {
            return;
        }
        mStarted = true;
        try {
            mPower = (PowerManager) mCtx.getSystemService(Context.POWER_SERVICE);
        } catch (Exception e) {
            Log.w(TAG, "no power service; thermal stays unknown", e);
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(Intent.ACTION_POWER_CONNECTED);
        filter.addAction(Intent.ACTION_POWER_DISCONNECTED);
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                mCtx.registerReceiver(mPowerEdge, filter, Context.RECEIVER_NOT_EXPORTED);
            } else {
                mCtx.registerReceiver(mPowerEdge, filter);
            }
        } catch (Exception e) {
            Log.w(TAG, "power-edge receiver not registered", e);
        }
        mHandler.post(mTick);
        Log.i(TAG, "device-state push started (30 s cadence)");
    }

    public synchronized void stop() {
        if (!mStarted) {
            return;
        }
        mStarted = false;
        mHandler.removeCallbacks(mTick);
        try {
            mCtx.unregisterReceiver(mPowerEdge);
        } catch (Exception ignored) {
        }
    }

    /** True once the engine frame loop reports itself up. */
    public static boolean engineReachable() {
        try {
            return nativeEngineUp();
        } catch (Throwable unused) {
            return false;
        }
    }

    private void sample(String reason) {
        int percent = -1;
        boolean charging = false;
        try {
            Intent battery = mCtx.registerReceiver(null,
                    new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (battery != null) {
                int level = battery.getIntExtra("level", -1);
                int scale = battery.getIntExtra("scale", -1);
                if (level >= 0 && scale > 0) {
                    percent = Math.round((level * 100.0f) / scale);
                    if (percent > 100) {
                        percent = 100;
                    }
                }
                int status = battery.getIntExtra("status", -1);
                charging = status == BATTERY_STATUS_CHARGING
                        || status == BATTERY_STATUS_FULL;
            }
        } catch (Exception e) {
            Log.w(TAG, "battery read failed", e);
        }

        int thermal = -1;
        if (mPower != null) {
            try {
                thermal = mPower.getCurrentThermalStatus();
            } catch (Exception e) {
                Log.w(TAG, "thermal read failed", e);
            }
        }

        boolean accepted = pushBattery(percent, charging);
        pushThermal(thermal);
        if (accepted && !mSawEngine) {
            mSawEngine = true;
            Log.i(TAG, "engine is up; device-state inputs live (battery "
                    + percent + "% charging=" + charging
                    + " thermal=" + thermal + ")");
        } else if (!accepted && !mSawEngine && "tick".equals(reason)) {
            Log.d(TAG, "engine not up yet; dropped sample battery="
                    + percent + " thermal=" + thermal);
        }
    }

    private boolean pushBattery(int percent, boolean charging) {
        try {
            return nativePushBattery(percent, charging);
        } catch (Throwable t) {
            logBridgeMissingOnce(t);
            return false;
        }
    }

    private void pushThermal(int status) {
        try {
            nativePushThermal(status);
        } catch (Throwable t) {
            logBridgeMissingOnce(t);
        }
    }

    private void logBridgeMissingOnce(Throwable t) {
        if (!mBridgeMissingLogged) {
            mBridgeMissingLogged = true;
            Log.i(TAG, "bridge natives absent (slice D); running reduced: "
                    + t.getClass().getSimpleName());
        }
    }

    /** Visible for tests. */
    boolean sawEngine() {
        return mSawEngine;
    }
}
