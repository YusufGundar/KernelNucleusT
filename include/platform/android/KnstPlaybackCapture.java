package com.knst.print;

import android.content.Context;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioPlaybackCaptureConfiguration;
import android.media.AudioRecord;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import java.io.FileOutputStream;

public class KnstPlaybackCapture {
    private static MediaProjection sProjection = null;

    /** Call once from Application.onCreate() or first Activity. */
    public static void init(android.content.Context ctx) {
        KnstAppContextHolder.set(ctx);
    }

    public static void setProjection(int resultCode, Intent data) {
        if (Build.VERSION.SDK_INT < 29) return;
        if (resultCode != android.app.Activity.RESULT_OK || data == null) {
            sProjection = null;
            return;
        }
        if (sProjection != null) { try { sProjection.stop(); } catch (Exception ignored) {} }
        Context ctx = KnstAppContextHolder.get();
        if (ctx == null) return;
        MediaProjectionManager mpm = (MediaProjectionManager) ctx.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        if (mpm == null) return;
        sProjection = mpm.getMediaProjection(resultCode, data);
    }

    public static boolean hasProjection() { return sProjection != null; }

    /**
     * @return byte[] raw PCM (little-endian, interleaved) or null on error.
     * sfmt: 0 = i16, 2 = f32 (matching C++ capture_format::sample_t enum)
     */
    public static byte[] capture(String outPath, int sampleRate, int channels,
                                  int sfmt, int totalFrames) {
        if (Build.VERSION.SDK_INT < 29 || sProjection == null) return null;
        try {
            AudioPlaybackCaptureConfiguration cfg =
                new AudioPlaybackCaptureConfiguration.Builder(sProjection)
                    .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                    .addMatchingUsage(AudioAttributes.USAGE_GAME)
                    .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                    .build();

            int channelMask = (channels == 1)
                ? AudioFormat.CHANNEL_IN_MONO
                : AudioFormat.CHANNEL_IN_STEREO;
            int encoding = (sfmt == 2)
                ? AudioFormat.ENCODING_PCM_FLOAT
                : AudioFormat.ENCODING_PCM_16BIT;
            int minBuf = AudioRecord.getMinBufferSize(sampleRate, channelMask, encoding);
            if (minBuf <= 0) minBuf = sampleRate * 2;

            AudioRecord record = new AudioRecord.Builder()
                .setAudioFormat(new AudioFormat.Builder()
                    .setEncoding(encoding)
                    .setSampleRate(sampleRate)
                    .setChannelMask(channelMask)
                    .build())
                .setBufferSizeInBytes(Math.max(minBuf * 2, 16384))
                .setAudioPlaybackCaptureConfig(cfg)
                .build();

            if (record.getState() != AudioRecord.STATE_INITIALIZED) {
                record.release();
                return null;
            }

            int bps = (sfmt == 2) ? 4 : 2;
            int totalBytes = totalFrames * channels * bps;
            byte[] out = new byte[totalBytes];
            int offset = 0;

            record.startRecording();
            long deadline = System.currentTimeMillis() +
                (totalFrames * 1000L / Math.max(1, sampleRate)) + 5000;
            while (offset < totalBytes && System.currentTimeMillis() < deadline) {
                int r = record.read(out, offset, totalBytes - offset,
                    (sfmt == 2) ? AudioRecord.READ_BLOCKING : AudioRecord.READ_BLOCKING);
                if (r > 0) offset += r;
            }
            record.stop();
            record.release();

            if (offset < totalBytes) {
                // Pad with silence
                for (int i = offset; i < totalBytes; ++i) out[i] = 0;
            }
            try (FileOutputStream fos = new FileOutputStream(outPath)) {
                fos.write(out);
            }
            return out;
        } catch (Exception ex) {
            return null;
        }
    }
}
