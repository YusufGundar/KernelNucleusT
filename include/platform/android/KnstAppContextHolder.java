package com.knst.print;

import android.content.Context;

/**
 * App-level Context holder.
 *
 * Must be its own file because:
 *   1. It's public (accessed from other packages / user app)
 *   2. Java requires every public class to be in a file with a matching name
 *
 * Call KnstPlaybackCapture.init(this) from Application.onCreate()
 * or from your Activity — that routes through here.
 */
public final class KnstAppContextHolder {
    private static Context sCtx;

    private KnstAppContextHolder() {}

    public static void set(Context c) {
        sCtx = (c == null) ? null : c.getApplicationContext();
    }

    public static Context get() {
        return sCtx;
    }
}