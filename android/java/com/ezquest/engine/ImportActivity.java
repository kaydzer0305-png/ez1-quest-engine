package com.ezquest.engine;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Imports staged retail folders into the shared depot layout.
 *
 * The user pushes their Steam folders to
 * {@code Download/EZQuest-<Game>-Import/<depot>} (adb recipe shown
 * on-screen, or plain USB/MTP copy); this activity validates the staging
 * area, promotes it into {@code srceng/common/} with progress and cancel,
 * then offers to launch the engine. UI is programmatic: no resources.
 */
public class ImportActivity extends Activity {
    private static final String TAG = "EZQuest-Import";

    private GameProfile mProfile;
    private TextView mReport;
    private TextView mStatus;
    private ProgressBar mProgress;
    private Button mStart;
    private Button mCancelBtn;
    private Button mLaunch;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private Thread mWorker;
    private Cancel mCancel = new Cancel();

    public static Intent intentFor(Context context) {
        return new Intent(context, ImportActivity.class);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        GameProfile profile;
        try {
            profile = GameProfile.forContext(this);
        } catch (Exception e) {
            Log.w(TAG, "no profile; defaulting to hl2", e);
            profile = GameProfile.HL2;
        }
        mProfile = profile;

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setBackgroundColor(Color.BLACK);
        int pad = (int) (20 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Import " + mProfile.displayName + " content");
        title.setTextColor(Color.WHITE);
        title.setTextSize(20);
        layout.addView(title);

        mReport = new TextView(this);
        mReport.setTextColor(Color.LTGRAY);
        mReport.setTextSize(14);
        mReport.setPadding(0, pad / 2, 0, 0);
        layout.addView(mReport);

        mProgress = new ProgressBar(this);
        mProgress.setMax(1000);
        mProgress.setVisibility(View.GONE);
        mProgress.setPadding(0, pad / 2, 0, 0);
        layout.addView(mProgress);

        mStatus = new TextView(this);
        mStatus.setTextColor(Color.WHITE);
        mStatus.setTextSize(14);
        mStatus.setPadding(0, pad / 2, 0, 0);
        layout.addView(mStatus);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        row.setPadding(0, pad / 2, 0, 0);

        mStart = new Button(this);
        mStart.setText("Import");
        mStart.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startImport();
            }
        });
        row.addView(mStart);

        mCancelBtn = new Button(this);
        mCancelBtn.setText("Cancel");
        mCancelBtn.setEnabled(false);
        mCancelBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                mCancel.cancel();
                setStatus("Cancelling…");
            }
        });
        row.addView(mCancelBtn);

        mLaunch = new Button(this);
        mLaunch.setText("Launch");
        mLaunch.setVisibility(View.GONE);
        mLaunch.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startActivity(new Intent(ImportActivity.this, EngineActivity.class));
                finish();
            }
        });
        row.addView(mLaunch);
        layout.addView(row);

        setContentView(layout);
        refresh();
    }

    @Override
    protected void onDestroy() {
        mCancel.cancel();
        ContentImport.end();
        super.onDestroy();
    }

    private void refresh() {
        ContentPresence shared = ContentPresence.check(SharedContent.commonRoot(), mProfile);
        StringBuilder sb = new StringBuilder();
        sb.append(shared.headline).append("\n\n");
        for (ContentPresence.DepotStatus depot : shared.depots) {
            sb.append(depot.present ? "[ok] " : "[missing] ")
                    .append(depot.depot.dirName).append("/ — ")
                    .append(depot.depot.displayName).append('\n');
        }
        if (!shared.readyToLaunch()) {
            sb.append('\n').append(shared.problem()).append("\n\n");
            sb.append("Push these folders:\n")
                    .append(SharedContent.stagingMtpPath(mProfile))
                    .append("\n\nor via adb:\n")
                    .append(SharedContent.adbPushInstruction(mProfile))
                    .append("\n\nFree: ")
                    .append(SharedContent.humanBytes(SharedContent.freeBytes()));
        }
        mReport.setText(sb.toString());
        mStart.setEnabled(!shared.readyToLaunch() && !ContentImport.isRunning());
        if (shared.readyToLaunch()) {
            mLaunch.setVisibility(View.VISIBLE);
            setStatus("Ready — launch when you are.");
        }
    }

    private void startImport() {
        if (!ContentImport.tryBegin()) {
            Toast.makeText(this, "An import is already running.", Toast.LENGTH_SHORT).show();
            return;
        }
        mCancel = new Cancel();
        mStart.setEnabled(false);
        mCancelBtn.setEnabled(true);
        mLaunch.setVisibility(View.GONE);
        mProgress.setVisibility(View.VISIBLE);
        mProgress.setProgress(0);
        setStatus("Measuring…");
        final Cancel cancel = mCancel;
        final GameProfile profile = mProfile;
        mWorker = new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    long bytes = ContentImport.run(ImportActivity.this, profile,
                            SharedContent.stagingDir(profile),
                            SharedContent.commonRoot(),
                            new ContentImport.Listener() {
                                @Override
                                public void onProgress(final long done, final long total) {
                                    mMain.post(new Runnable() {
                                        @Override
                                        public void run() {
                                            if (total > 0) {
                                                mProgress.setProgress(
                                                        (int) (done * 1000 / total));
                                                setStatus("Copying… "
                                                        + SharedContent.humanBytes(done)
                                                        + " / "
                                                        + SharedContent.humanBytes(total));
                                            }
                                        }
                                    });
                                }

                                @Override
                                public void onDepotDone(ContentDepot depot) {
                                }
                            }, cancel);
                    postDone("Imported " + SharedContent.humanBytes(bytes) + ".", true);
                } catch (final java.util.concurrent.CancellationException e) {
                    postDone("Import cancelled; partial depot removed.", false);
                } catch (final Exception e) {
                    Log.e(TAG, "import failed", e);
                    postDone("Import failed: " + e.getMessage(), false);
                } finally {
                    ContentImport.end();
                }
            }
        }, "ezquest-import");
        mWorker.start();
    }

    private void postDone(final String message, final boolean success) {
        mMain.post(new Runnable() {
            @Override
            public void run() {
                ContentImport.end();
                mCancelBtn.setEnabled(false);
                mProgress.setVisibility(View.GONE);
                setStatus(message);
                Toast.makeText(ImportActivity.this, message, Toast.LENGTH_LONG).show();
                refresh();
                if (success) {
                    mLaunch.setVisibility(View.VISIBLE);
                }
            }
        });
    }

    private void setStatus(String status) {
        mStatus.setText(status);
    }
}
