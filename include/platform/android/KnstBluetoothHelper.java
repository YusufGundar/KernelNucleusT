// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


// =============================================================================
//  KnstBluetoothHelper.java
//  ----------------------------------------------------------------------------
//  JNI helper for knst_bt::bluetooth on Android.
//
//  Android's BluetoothAdapter provides:
//    ✓ Adapter info (name, address, enabled, discoverable)
//    ✓ Bonded device list (BluetoothDevice via getBondedDevices)
//    ✓ Classic discovery (startDiscovery + ACTION_FOUND receiver)
//    ✓ Pair (createBond) / Unpair (removeBond via reflection)
//    ✓ Battery level (BluetoothDevice.getBatteryLevel, API 30+)
//    ✓ isConnected() (API 31+)
//    ✗ Programmatic power toggle (Android 13+ requires user)
//    ✗ Set discoverable / pairable / adapter name (deprecated or blocked)
//    ✗ Trust/untrust (bond = trusted in Android's model)
//    ✗ Connect/disconnect without GATT profile
//    ✗ RSSI outside active scan
//    ✗ PIN entry (requires BLUETOOTH_PRIVILEGED, system apps only)
// =============================================================================

package com.knst.devices;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

public final class KnstBluetoothHelper {

    private static Context s_ctx = null;
    private static BroadcastReceiver s_receiver = null;
    private static volatile boolean s_discovering = false;
    private static final ConcurrentHashMap<String, BluetoothDevice> s_discovered
        = new ConcurrentHashMap<>();

    private KnstBluetoothHelper() {}

    public static void setContext(Context ctx) {
        if (ctx != null) s_ctx = ctx.getApplicationContext();
    }

    private static BluetoothAdapter getAdapter() {
        if (s_ctx == null) return null;
        try {
            BluetoothManager bm = (BluetoothManager)
                s_ctx.getSystemService(Context.BLUETOOTH_SERVICE);
            return bm != null ? bm.getAdapter() : null;
        } catch (Throwable t) { return null; }
    }

    // ---- helpers ------------------------------------------------------------

    private static String safeName(BluetoothDevice d) {
        try { String n = d.getName(); return n != null ? n : ""; }
        catch (Throwable t) { return ""; }
    }

    private static String safeAddress(BluetoothDevice d) {
        try { String a = d.getAddress(); return a != null ? a : ""; }
        catch (Throwable t) { return ""; }
    }

    private static int safeType(BluetoothDevice d) {
        try { return d.getType(); } catch (Throwable t) { return 0; }
    }

    private static int safeClass(BluetoothDevice d) {
        try {
            BluetoothClass bc = d.getBluetoothClass();
            return bc != null ? bc.getDeviceClass() : 0;
        } catch (Throwable t) { return 0; }
    }

    private static int safeBondState(BluetoothDevice d) {
        try { return d.getBondState(); } catch (Throwable t) { return 10; }
    }

    private static int safeIsConnected(BluetoothDevice d) {
        try {
            java.lang.reflect.Method m =
                BluetoothDevice.class.getMethod("isConnected");
            Object r = m.invoke(d);
            return (r instanceof Boolean && (Boolean) r) ? 1 : 0;
        } catch (Throwable t) {
            return safeBondState(d) == 12 ? 1 : 0;
        }
    }

    // ---- adapter ------------------------------------------------------------

    // [name, address, isEnabled(0/1), scanMode(int)]
    // scanMode: 20=NONE, 21=CONNECTABLE, 23=CONNECTABLE_DISCOVERABLE
    public static Object[] getAdapterInfo() {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null) return null;
            String name = null, addr = null;
            try { name = a.getName(); } catch (Throwable ignore) {}
            try { addr = a.getAddress(); } catch (Throwable ignore) {}
            int enabled = 0, scan_mode = 20;
            try { enabled = a.isEnabled() ? 1 : 0; } catch (Throwable ignore) {}
            try { scan_mode = a.getScanMode(); } catch (Throwable ignore) {}
            return new Object[] {
                name != null ? name : "",
                addr != null ? addr : "",
                Integer.valueOf(enabled),
                Integer.valueOf(scan_mode)
            };
        } catch (Throwable t) { return null; }
    }

    // ---- bonded devices -----------------------------------------------------

    // [name, address, type, class, bondState, isConnected] × N
    public static Object[] getBondedDevices() {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null) return null;
            Set<BluetoothDevice> set = a.getBondedDevices();
            if (set == null) return new Object[0];
            Object[] out = new Object[set.size() * 6];
            int i = 0;
            for (BluetoothDevice d : set) {
                out[i++] = safeName(d);
                out[i++] = safeAddress(d);
                out[i++] = Integer.valueOf(safeType(d));
                out[i++] = Integer.valueOf(safeClass(d));
                out[i++] = Integer.valueOf(safeBondState(d));
                out[i++] = Integer.valueOf(safeIsConnected(d));
            }
            return out;
        } catch (Throwable t) { return null; }
    }

    // [name, address, type, class, bondState, isConnected] or null
    public static Object[] getDeviceInfo(String address) {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || address == null) return null;
            BluetoothDevice d = a.getRemoteDevice(address);
            if (d == null) return null;
            return new Object[] {
                safeName(d), safeAddress(d),
                Integer.valueOf(safeType(d)),
                Integer.valueOf(safeClass(d)),
                Integer.valueOf(safeBondState(d)),
                Integer.valueOf(safeIsConnected(d))
            };
        } catch (Throwable t) { return null; }
    }

    public static int getBondState(String address) {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || address == null) return 10;
            BluetoothDevice d = a.getRemoteDevice(address);
            return d != null ? d.getBondState() : 10;
        } catch (Throwable t) { return 10; }
    }

    // -1 if unknown, 0..100 otherwise
    public static int getBatteryLevel(String address) {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || address == null) return -1;
            BluetoothDevice d = a.getRemoteDevice(address);
            if (d == null) return -1;
            try {
                java.lang.reflect.Method m =
                    BluetoothDevice.class.getMethod("getBatteryLevel");
                Object r = m.invoke(d);
                return (r instanceof Integer) ? ((Integer) r).intValue() : -1;
            } catch (Throwable t) { return -1; }
        } catch (Throwable t) { return -1; }
    }

    // ---- discovery ----------------------------------------------------------

    public static int startDiscovery() {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || !a.isEnabled()) return 0;
            synchronized (KnstBluetoothHelper.class) {
                if (s_discovering) return 1;
                if (s_ctx == null) return 0;
                s_discovered.clear();
                if (s_receiver == null) {
                    s_receiver = new BroadcastReceiver() {
                        @Override
                        public void onReceive(Context c, Intent intent) {
                            if (BluetoothDevice.ACTION_FOUND.equals(intent.getAction())) {
                                BluetoothDevice d = intent.getParcelableExtra(
                                    BluetoothDevice.EXTRA_DEVICE);
                                if (d != null) {
                                    try { s_discovered.put(d.getAddress(), d); }
                                    catch (Throwable ignore) {}
                                }
                            }
                        }
                    };
                }
                try {
                    IntentFilter f = new IntentFilter(BluetoothDevice.ACTION_FOUND);
                    if (android.os.Build.VERSION.SDK_INT >= 33) {
                        s_ctx.registerReceiver(s_receiver, f,
                            Context.RECEIVER_NOT_EXPORTED);
                    } else {
                        s_ctx.registerReceiver(s_receiver, f);
                    }
                } catch (Throwable ignore) {
                    return 0;
                }
                boolean ok = false;
                try { ok = a.startDiscovery(); } catch (Throwable t) {}
                if (!ok) {
                    try { s_ctx.unregisterReceiver(s_receiver); }
                    catch (Throwable ignore) {}
                    return 0;
                }
                s_discovering = true;
                return 1;
            }
        } catch (Throwable t) { return 0; }
    }

    public static void stopDiscovery() {
        try {
            BluetoothAdapter a = getAdapter();
            if (a != null) {
                try { a.cancelDiscovery(); } catch (Throwable ignore) {}
            }
        } catch (Throwable t) {}
        synchronized (KnstBluetoothHelper.class) {
            if (s_receiver != null && s_ctx != null) {
                try { s_ctx.unregisterReceiver(s_receiver); }
                catch (Throwable ignore) {}
            }
            s_discovering = false;
        }
    }

    public static Object[] getDiscoveredDevices() {
        try {
            int n = s_discovered.size();
            Object[] out = new Object[n * 6];
            int i = 0;
            for (BluetoothDevice d : s_discovered.values()) {
                out[i++] = safeName(d);
                out[i++] = safeAddress(d);
                out[i++] = Integer.valueOf(safeType(d));
                out[i++] = Integer.valueOf(safeClass(d));
                out[i++] = Integer.valueOf(safeBondState(d));
                out[i++] = Integer.valueOf(safeIsConnected(d));
            }
            return out;
        } catch (Throwable t) { return null; }
    }

    // ---- pair / unpair ------------------------------------------------------

    public static int createBond(String address) {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || address == null) return 0;
            BluetoothDevice d = a.getRemoteDevice(address);
            if (d == null) return 0;
            int state = d.getBondState();
            if (state == BluetoothDevice.BOND_BONDED)  return 1;
            if (state == BluetoothDevice.BOND_BONDING) return 1;   // in progress
            try {
                java.lang.reflect.Method m =
                    BluetoothDevice.class.getMethod("createBond");
                Object r = m.invoke(d);
                return (r instanceof Boolean && (Boolean) r) ? 1 : 0;
            } catch (Throwable t) { return 0; }
        } catch (Throwable t) { return 0; }
    }

    public static int removeBond(String address) {
        try {
            BluetoothAdapter a = getAdapter();
            if (a == null || address == null) return 0;
            BluetoothDevice d = a.getRemoteDevice(address);
            if (d == null) return 1;
            if (d.getBondState() == BluetoothDevice.BOND_NONE) return 1;
            try {
                java.lang.reflect.Method m =
                    BluetoothDevice.class.getMethod("removeBond");
                Object r = m.invoke(d);
                return (r instanceof Boolean && (Boolean) r) ? 1 : 0;
            } catch (Throwable t) { return 0; }
        } catch (Throwable t) { return 0; }
    }
}