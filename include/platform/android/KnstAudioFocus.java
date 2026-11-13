package com.knst.print;

import android.content.Context;
import android.media.AudioAttributes;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Build;

public final class KnstAudioFocus {
    private static AudioManager sAm;
    private static AudioFocusRequest sRequest;
    private static boolean sHasFocus;

    private KnstAudioFocus() {}

    public static void init(Context ctx) {
        sAm = (AudioManager) ctx.getApplicationContext().getSystemService(Context.AUDIO_SERVICE);
    }

    public static synchronized boolean request() {
        if (sAm == null) return false;
        if (sHasFocus) return true;
        if (Build.VERSION.SDK_INT >= 26) {
            AudioAttributes attrs = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
            sRequest = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
                .setAudioAttributes(attrs)
                .setWillPauseWhenDucked(false)
                .build();
            sHasFocus = sAm.requestAudioFocus(sRequest) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        } else {
            sHasFocus = sAm.requestAudioFocus(null, AudioManager.STREAM_MUSIC,
                AudioManager.AUDIOFOCUS_GAIN) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        }
        return sHasFocus;
    }

    public static synchronized void abandon() {
        if (sAm == null) return;
        if (Build.VERSION.SDK_INT >= 26 && sRequest != null) {
            sAm.abandonAudioFocusRequest(sRequest);
            sRequest = null;
        } else {
            sAm.abandonAudioFocus(null);
        }
        sHasFocus = false;
    }

    public static synchronized boolean hasFocus() { return sHasFocus; }
}