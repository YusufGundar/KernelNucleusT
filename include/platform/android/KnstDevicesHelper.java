// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

// =============================================================================
//  KnstDevicesHelper.java
// =============================================================================

package com.knst.devices;

import android.content.Context;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;

public final class KnstDevicesHelper {

    private static Context s_ctx = null;

    private KnstDevicesHelper() {}

    public static void setContext(Context ctx) {
        if (ctx != null) s_ctx = ctx;
    }

    public static Object[] getAudioDevicesInfo() {
        if (s_ctx == null) return null;
        try {
            AudioManager am =
                (AudioManager) s_ctx.getSystemService(Context.AUDIO_SERVICE);
            if (am == null) return null;

            AudioDeviceInfo[] devices = null;

            // Path 1: getDevices() (no-arg) via reflection
            try {
                java.lang.reflect.Method m =
                    AudioManager.class.getMethod("getDevices");
                devices = (AudioDeviceInfo[]) m.invoke(am);
            } catch (Throwable ignore) {}

            // Path 2: getDevices(int) with GET_DEVICES_ALL=3
            if (devices == null || devices.length == 0) {
                try {
                    java.lang.reflect.Method m =
                        AudioManager.class.getMethod("getDevices", int.class);
                    devices = (AudioDeviceInfo[]) m.invoke(am, Integer.valueOf(3));
                } catch (Throwable ignore) {}
            }

            if (devices == null) return null;

            int n = devices.length;
            Object[] out = new Object[n * 5];
            for (int i = 0; i < n; i++) {
                AudioDeviceInfo d = devices[i];
                out[i * 5 + 0] = Integer.valueOf(d.getType());
                CharSequence pn = d.getProductName();
                out[i * 5 + 1] = (pn != null) ? pn.toString() : "";
                out[i * 5 + 2] = Boolean.valueOf(d.isSink());
                out[i * 5 + 3] = Boolean.valueOf(d.isSource());
                out[i * 5 + 4] = Integer.valueOf(d.getId());
            }
            return out;
        } catch (Throwable ignore) {
            return null;
        }
    }

    // ─────────────────────────────────────────────────────────────
    //  getWifiInfo — SSID, BSSID, RSSI, link speed, frequency
    //
    //  Returns flat Object[] of 6 elements or null on failure:
    //    [0] String   SSID (quotes stripped)
    //    [1] String   BSSID (MAC, may be 02:00:00:00:00:00 on 6+)
    //    [2] Integer  RSSI (dBm, negative)
    //    [3] Integer  link speed (Mbps)
    //    [4] Integer  frequency (MHz)
    //    [5] Integer  IPv4 address (packed int)
    // ─────────────────────────────────────────────────────────────
    public static Object[] getWifiInfo() {
        if (s_ctx == null) return null;
        try {
            android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager)
                s_ctx.getApplicationContext().getSystemService(
                    Context.WIFI_SERVICE);
            if (wm == null) return null;
            android.net.wifi.WifiInfo wi = wm.getConnectionInfo();
            if (wi == null) return null;

            String ssid = wi.getSSID();
            if (ssid != null && ssid.length() >= 2
                && ssid.startsWith("\"") && ssid.endsWith("\"")) {
                ssid = ssid.substring(1, ssid.length() - 1);
            }

            Object[] out = new Object[6];
            out[0] = (ssid != null) ? ssid : "";
            String bssid = wi.getBSSID();
            out[1] = (bssid != null) ? bssid : "";
            out[2] = Integer.valueOf(wi.getRssi());
            out[3] = Integer.valueOf(wi.getLinkSpeed());
            out[4] = Integer.valueOf(wi.getFrequency());
            out[5] = Integer.valueOf(wi.getIpAddress());
            return out;
        } catch (Throwable ignore) {
            return null;
        }
    }

    // ─────────────────────────────────────────────────────────────
    //  getSystemSerial — best-effort device serial
    //
    //  Android 10+ hides ro.serialno / ro.boot.serialno from apps
    //  without READ_PHONE_STATE. Build.getSerial() is the official
    //  API. Returns "" if unavailable.
    // ─────────────────────────────────────────────────────────────
    public static String getSystemSerial() {
        try {
            if (android.os.Build.VERSION.SDK_INT >= 26) {
                return android.os.Build.getSerial();
            }
        } catch (Throwable ignore) {}

        // Reflection fallback for older APIs
        try {
            java.lang.reflect.Field f = android.os.Build.class.getField("SERIAL");
            Object v = f.get(null);
            return (v != null) ? v.toString() : "";
        } catch (Throwable ignore) {}

        return "";
    }

    public static String[] getAppPaths() {
        if (s_ctx == null) return null;
        try {
            String files = "";
            String cache = "";
            String ext   = "";
            String home  = "";
            try {
                java.io.File f = s_ctx.getFilesDir();
                if (f != null) files = f.getAbsolutePath();
            } catch (Throwable ignore) {}
            try {
                java.io.File f = s_ctx.getCacheDir();
                if (f != null) cache = f.getAbsolutePath();
            } catch (Throwable ignore) {}
            try {
                java.io.File f = s_ctx.getExternalFilesDir(null);
                if (f != null) ext = f.getAbsolutePath();
            } catch (Throwable ignore) {}

            if (!files.isEmpty()) {
                int lastSlash = files.lastIndexOf('/');
                if (lastSlash > 0) home = files.substring(0, lastSlash);
            }
            return new String[] { files, cache, ext, home };
        } catch (Throwable ignore) {
            return null;
        }
    }
}