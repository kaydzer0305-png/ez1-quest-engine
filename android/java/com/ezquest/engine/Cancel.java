package com.ezquest.engine;

import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.CancellationException;

/** Cooperative cancellation token for content scans and imports. */
public final class Cancel {
    private final AtomicBoolean mCancelled = new AtomicBoolean(false);

    public void cancel() {
        mCancelled.set(true);
    }

    public boolean isCancelled() {
        return mCancelled.get();
    }

    public void throwIfCancelled() {
        if (mCancelled.get()) {
            throw new CancellationException("stopped by user");
        }
    }
}
