// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

package com.knst.window;

import android.app.NativeActivity;
import android.content.Context;
import android.media.AudioManager;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

/**
 * KnstActivity — NativeActivity subclass that owns an InputConnection
 * so the native side receives ALL text from the soft keyboard: long-
 * press popup chars (u -> ü), IME composition, emoji, autocorrect,
 * and paste.
 *
 * IME policy:
 *   - Keyboard hidden on launch, view not focused.
 *   - showKeyboard()  ->  VISIBLE + ADJUST_RESIZE + requestFocus +
 *                         SHOW_FORCED, retried 3 times.
 *   - hideKeyboard()  ->  hideSoftInput + clearFocus + ALWAYS_HIDDEN.
 *
 * Volume keys: setVolumeControlStream(STREAM_MUSIC) so they change the
 * media stream instead of the ringer.
 *
 * Back gesture: Android 13+ gesture nav doesn't send AKEYCODE_BACK —
 * it calls onBackPressed() instead. We forward it to native.
 */
public class KnstActivity extends NativeActivity {

    static { System.loadLibrary("knst_app"); }

    // Native callbacks (implemented in libknst_app.so).
    public static native void nativeCommitText(String utf8Text);
    public static native void nativeDeleteSurrounding(int before, int after);
    public static native void nativeSetComposing(String utf8Text);
    public static native void nativeFinishComposing();
    public static native void nativeEditorAction(int actionCode);
    public static native void nativeInputConnectionAttached();
    public static native void nativeInputConnectionDetached();
    public static native boolean nativeBackPressed();

    private static volatile KnstActivity sActivity;
    private static volatile KnstInputView sInputView;
    private static volatile InputMethodManager sImm;
    private static volatile boolean sUserWantsKeyboard = false;

    // --- Public API ---
    // Called from the native thread; every method posts to the UI
    // thread because Android only lets the UI thread touch views.


    public static boolean showKeyboard() {
        final KnstActivity  act  = sActivity;
        final KnstInputView view = sInputView;
        if (act == null || view == null) return false;

        sUserWantsKeyboard = true;

        act.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (sImm == null) {
                    sImm = (InputMethodManager)
                        act.getSystemService(Context.INPUT_METHOD_SERVICE);
                }
                if (sImm == null) return;

                // Flip the window policy to VISIBLE — ALWAYS_HIDDEN
                // blocks the IME entirely.
                act.getWindow().setSoftInputMode(
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_VISIBLE
                  | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);

                // Focus + IME request. SHOW_FORCED bypasses the window
                // policy so we can pop the keyboard even from a hidden
                // state.
                view.requestFocus();
                view.performClick();
                sImm.showSoftInput(view, InputMethodManager.SHOW_FORCED);

                // Retry up to 3 times to let focus settle.
                retryShow(view, 0);
            }
        });
        return true;
    }

    private static void retryShow(final KnstInputView view, final int attempt) {
        if (view == null || attempt >= 3) return;
        view.postDelayed(new Runnable() {
            @Override public void run() {
                if (!sUserWantsKeyboard) return;
                if (sImm == null) return;
                view.requestFocus();
                sImm.showSoftInput(view, InputMethodManager.SHOW_FORCED);
                retryShow(view, attempt + 1);
            }
        }, 200L);
    }

    public static boolean hideKeyboard() {
        final KnstActivity  act  = sActivity;
        final KnstInputView view = sInputView;
        if (act == null || view == null) return false;
        sUserWantsKeyboard = false;
        act.runOnUiThread(new Runnable() {
            @Override public void run() {
                if (sImm != null) {
                    sImm.hideSoftInputFromWindow(view.getWindowToken(),InputMethodManager.HIDE_IMPLICIT_ONLY);
                    sImm.hideSoftInputFromWindow(view.getWindowToken(), 0);
                }
                view.clearFocus();
                act.getWindow().setSoftInputMode(
                    WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);

                // Drop the native flag so physical keyboard text can
                // flow through the getUnicodeChar path again.
                nativeInputConnectionDetached();
            }
        });
        return true;
    }

    // --- Lifecycle ---


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Volume keys change the media stream.
        setVolumeControlStream(AudioManager.STREAM_MUSIC);

        // Keyboard hidden on launch.
        getWindow().setSoftInputMode(
            WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);

        sActivity = this;
        sImm = (InputMethodManager)
            getSystemService(Context.INPUT_METHOD_SERVICE);

        ViewGroup root = (ViewGroup) findViewById(android.R.id.content);

        sInputView = new KnstInputView(this);
        sInputView.setBackground(null);
        sInputView.setCursorVisible(false);

        // Full width, 1 px tall, bottom-aligned. The IME needs a real
        // position for its cursor.
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, 1);
        lp.gravity = Gravity.BOTTOM;

        // Add at index 0 so the EditText sits at the BOTTOM of the
        // Z-order. The NativeContentView (SurfaceView) stays on top and
        // receives the touch events; the EditText only holds focus for
        // the IME.
        //
        // Default behavior (no index) would put the EditText on top,
        // which makes it send CANCEL on multi-touch.
        root.addView(sInputView, 0, lp);
    }

    // Gesture-nav back. If native returns true the event is queued and
    // the main loop performs a clean teardown. If false (native thread
    // already gone), the framework handles it and the app closes.
    @Override
    public void onBackPressed() {
        if (!nativeBackPressed()) {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        // During a fast close/open cycle, the old instance's onDestroy
        // can run AFTER the new onCreate. Wiping the statics here would
        // kill the NEW activity — keyboard and Back would silently stop
        // working.
        //
        // Guard: only clear if this instance is still the active one.
        if (sActivity == this) {
            sActivity = null;
            sInputView = null;
            sImm = null;
        }
        super.onDestroy();
    }

    // --- The View that owns the InputConnection ---
    // EditText-based so the IME has something real to attach to. It's
    // invisible (1px tall, transparent) but the IME doesn't care.


    public static class KnstInputView extends EditText {
        public KnstInputView(Context ctx) {
            super(ctx);
            setBackground(null);
            setCursorVisible(false);

            // Do NOT consume touch — the parent (NativeContentView)
            // should receive it, otherwise multi-touch becomes CANCEL.
            setClickable(false);
            setLongClickable(false);
            setFocusableInTouchMode(true);  // needed for IME focus
        }

        @Override public boolean onCheckIsTextEditor() { return true; }

        // Don't consume touch events — pass them to the parent.
        // EditText would normally grab the touch, but the real touch
        // surface is the NativeContentView behind us.
        @Override
        public boolean onTouchEvent(android.view.MotionEvent event) {
            return false;
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
            outAttrs.inputType = EditorInfo.TYPE_CLASS_TEXT
                | EditorInfo.TYPE_TEXT_FLAG_MULTI_LINE
                | EditorInfo.TYPE_TEXT_FLAG_NO_SUGGESTIONS;
            outAttrs.imeOptions = EditorInfo.IME_ACTION_NONE
                | EditorInfo.IME_FLAG_NO_FULLSCREEN
                | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | EditorInfo.IME_FLAG_NO_ENTER_ACTION;

            nativeInputConnectionAttached();

            return new BaseInputConnection(this, false) {
                @Override public boolean commitText(CharSequence t, int p) {
                    if (t != null && t.length() > 0) nativeCommitText(t.toString());
                    return true;
                }
                @Override public boolean setComposingText(CharSequence t, int p) {
                    if (t != null) nativeSetComposing(t.toString());
                    return true;
                }
                @Override public boolean finishComposingText() {
                    nativeFinishComposing(); return true;
                }
                @Override public boolean deleteSurroundingText(int b, int a) {
                    nativeDeleteSurrounding(b, a); return true;
                }
                @Override public boolean performEditorAction(int a) {
                    nativeEditorAction(a); return true;
                }
            };
        }
    }
}