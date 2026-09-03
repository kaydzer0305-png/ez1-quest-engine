package com.ezquest.engine;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

/**
 * Fail-loud error screen shown when the engine cannot boot (missing
 * content, bad profile, native load failure). Replaces a black screen
 * with the reason, so on-device debugging doesn't need logcat.
 *
 * UI is built programmatically: the APK packages no resources.
 */
public class RecoveryActivity extends Activity {
    public static final String EXTRA_REASON =
            "com.ezquest.engine.EXTRA_REASON";
    public static final String EXTRA_MESSAGE =
            "com.ezquest.engine.EXTRA_MESSAGE";

    public static Intent intentFor(Context context, String reason, String message) {
        Intent intent = new Intent(context, RecoveryActivity.class);
        intent.putExtra(EXTRA_REASON, reason);
        intent.putExtra(EXTRA_MESSAGE, message);
        return intent;
    }

    public static void launch(Context context, String reason, String message) {
        try {
            context.startActivity(intentFor(context, reason, message));
        } catch (Exception ignored) {
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        String reason = getIntent().getStringExtra(EXTRA_REASON);
        String message = getIntent().getStringExtra(EXTRA_MESSAGE);
        if (reason == null) {
            reason = "startup";
        }
        if (message == null) {
            message = "The engine could not start.";
        }

        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setBackgroundColor(Color.BLACK);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        layout.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText("Source Quest — cannot start (" + reason + ")");
        title.setTextColor(Color.WHITE);
        title.setTextSize(20);
        title.setGravity(Gravity.CENTER);

        TextView body = new TextView(this);
        body.setText(message);
        body.setTextColor(Color.LTGRAY);
        body.setTextSize(15);
        body.setGravity(Gravity.CENTER);
        body.setPadding(0, pad / 2, 0, 0);

        layout.addView(title);
        layout.addView(body);

        Button export = new Button(this);
        export.setText("Export diagnostics");
        export.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String status = Diagnostics.exportStatusMessage(RecoveryActivity.this);
                Toast.makeText(RecoveryActivity.this, status, Toast.LENGTH_LONG).show();
            }
        });
        export.setPadding(0, pad, 0, 0);
        layout.addView(export);

        if ("content".equals(reason)) {
            Button importContent = new Button(this);
            importContent.setText("Import content");
            importContent.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    startActivity(ImportActivity.intentFor(RecoveryActivity.this));
                    finish();
                }
            });
            importContent.setPadding(0, pad / 2, 0, 0);
            layout.addView(importContent);
        }

        setContentView(layout);
    }
}
