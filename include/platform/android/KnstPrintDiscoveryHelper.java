// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


package com.knst.print;


import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.MulticastSocket;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.HashSet;
import java.util.Set;

import android.content.Context;
import android.net.nsd.NsdManager;
import android.net.wifi.WifiManager;
import android.net.nsd.NsdServiceInfo;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class KnstPrintDiscoveryHelper {
    private static Context sCtx;

    public static void setContext(Context ctx) { sCtx = ctx.getApplicationContext(); }

    /**
     * Scans all common printer service types.
     * Android requires a separate discovery listener per service type.
     */
    public static String[] discover(int timeoutMs) {
        if (sCtx == null) return new String[0];
        final NsdManager nsd = (NsdManager) sCtx.getSystemService(Context.NSD_SERVICE);
        if (nsd == null) return new String[0];

        final List<String> results = new ArrayList<>();
        final String[] types = {
            "_ipp._tcp",            // Modern IPP Everywhere / Mopria
            "_ipps._tcp",           // IPP over TLS
            "_printer._tcp",        // Legacy LPD
            "_pdl-datastream._tcp", // Port 9100 raw (most thermal/label printers)
            "_scanner._tcp"         // Scanners (multifunction)
        };

        final CountDownLatch done = new CountDownLatch(types.length);
        final List<NsdManager.DiscoveryListener> listeners = new ArrayList<>();

        for (final String type : types) {
            NsdManager.DiscoveryListener listener = new NsdManager.DiscoveryListener() {
                @Override public void onStartDiscoveryFailed(String t, int err) {
                    done.countDown();
                }
                @Override public void onStopDiscoveryFailed(String t, int err) {
                    done.countDown();
                }
                @Override public void onDiscoveryStarted(String t) { }
                @Override public void onDiscoveryStopped(String t) { }
                @Override public void onServiceFound(NsdServiceInfo info) {
                    nsd.resolveService(info, new NsdManager.ResolveListener() {
                        @Override public void onResolveFailed(NsdServiceInfo i, int err) { }
                        @Override public void onServiceResolved(NsdServiceInfo i) {
                            String host = (i.getHost() != null)
                                    ? i.getHost().getHostAddress() : "?";
                            String svcType = i.getServiceType();
                            int port = i.getPort();
                            String name = i.getServiceName();

                            // Pick the right scheme based on the service type.
                            String uri;
                            if (svcType.contains("_ipp._tcp")) {
                                uri = "ipp://" + host + ":" + port + "/ipp/print";
                            } else if (svcType.contains("_ipps._tcp")) {
                                uri = "ipps://" + host + ":" + port + "/ipp/print";
                            } else if (svcType.contains("_pdl-datastream._tcp")) {
                                uri = "socket://" + host + ":" + port;
                            } else if (svcType.contains("_printer._tcp")) {
                                uri = "lpd://" + host + "/ipp/print";
                            } else if (svcType.contains("_scanner._tcp")) {
                                // Scanner — not a printer, skip.
                                return;
                            } else {
                                uri = "ipp://" + host + ":" + port + "/ipp/print";
                            }

                            synchronized (results) {
                                results.add(name + "|" + host + "|" + port + "|" + uri);
                            }
                        }
                    });
                }
                @Override public void onServiceLost(NsdServiceInfo info) { }
            };
            listeners.add(listener);
            try {
                nsd.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, listener);
            } catch (Exception e) {
                done.countDown();
            }
        }

        try { done.await(timeoutMs, TimeUnit.MILLISECONDS); }
        catch (InterruptedException ie) { }

        for (NsdManager.DiscoveryListener l : listeners) {
            try { nsd.stopServiceDiscovery(l); } catch (Exception ignore) { }
        }

        // DEDUPE: keep the best URI per printer.
        // Priority: ipps > ipp > socket > lpd.
        Map<String, String[]> best = new LinkedHashMap<>();
        synchronized (results) {
            for (String entry : results) {
                String[] parts = entry.split("\\|", 4);
                if (parts.length < 4) continue;
                String name = parts[0];
                String uri = parts[3];
                int prio;
                if (uri.startsWith("ipps://"))         prio = 4;
                else if (uri.startsWith("ipp://"))     prio = 3;
                else if (uri.startsWith("socket://"))  prio = 2;
                else if (uri.startsWith("lpd://"))     prio = 1;
                else                                   prio = 0;

                String[] cur = best.get(name);
                if (cur == null) {
                    best.put(name, new String[]{entry, String.valueOf(prio)});
                } else if (prio > Integer.parseInt(cur[1])) {
                    best.put(name, new String[]{entry, String.valueOf(prio)});
                }
            }
        }

        List<String> deduped = new ArrayList<>();
        for (String[] v : best.values()) deduped.add(v[0]);
        return deduped.toArray(new String[0]);
    }

    /**
     * Diagnostic helper — lists every visible mDNS service.
     * Useful to see all mDNS traffic even when no print service is present.
     */
    public static String[] dumpAllServices(int timeoutMs) {
        if (sCtx == null) return new String[0];
        final NsdManager nsd = (NsdManager) sCtx.getSystemService(Context.NSD_SERVICE);
        if (nsd == null) return new String[0];

        // Try all common service types.
        final String[] types = {
            "_ipp._tcp", "_ipps._tcp", "_printer._tcp",
            "_pdl-datastream._tcp", "_scanner._tcp",
            "_http._tcp", "_https._tcp",
            "_googlecast._tcp", "_airplay._tcp", "_raop._tcp",
            "_smb._tcp", "_workstation._tcp"
        };

        final List<String> results = new ArrayList<>();
        final CountDownLatch done = new CountDownLatch(types.length);
        final List<NsdManager.DiscoveryListener> listeners = new ArrayList<>();

        for (final String type : types) {
            NsdManager.DiscoveryListener listener = new NsdManager.DiscoveryListener() {
                @Override public void onStartDiscoveryFailed(String t, int err) { done.countDown(); }
                @Override public void onStopDiscoveryFailed(String t, int err) { done.countDown(); }
                @Override public void onDiscoveryStarted(String t) { }
                @Override public void onDiscoveryStopped(String t) { }
                @Override public void onServiceFound(NsdServiceInfo info) {
                    synchronized (results) {
                        results.add(info.getServiceType() + "|" + info.getServiceName());
                    }
                }
                @Override public void onServiceLost(NsdServiceInfo info) { }
            };
            listeners.add(listener);
            try {
                nsd.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, listener);
            } catch (Exception e) { done.countDown(); }
        }
        try { done.await(timeoutMs, TimeUnit.MILLISECONDS); }
        catch (InterruptedException ie) { }
        for (NsdManager.DiscoveryListener l : listeners) {
            try { nsd.stopServiceDiscovery(l); } catch (Exception ignore) { }
        }
        synchronized (results) { return results.toArray(new String[0]); }
    }

    /**
     * Discovers eSCL (AirScan) scanners. Result format: "name|host|port".
     * The C++ side (knst_prn::scanner::list_all) calls this via JNI.
     */
    public static String[] discoverScanners(int timeoutMs) {
        //  Disable Android's multicast filter (WifiManager.MulticastLock).
        //   Without this the kernel drops all mDNS replies -- zero results.
        WifiManager wm = (WifiManager) sCtx.getSystemService(Context.WIFI_SERVICE);
        WifiManager.MulticastLock lock =
            (wm != null) ? wm.createMulticastLock("knst_mdns") : null;
        if (lock != null) {
            lock.setReferenceCounted(false);
            lock.acquire();
        }
        try {
            return discoverScannersRaw(timeoutMs);
        } finally {
            if (lock != null && lock.isHeld()) {
                lock.release();
            }
        }
    }

    private static String[] discoverScannersRaw(int timeoutMs) {
        if (sCtx == null) return new String[0];

        final Set<String> results = new HashSet<>(); // dedupe
        MulticastSocket socket = null;
        InetAddress group = null;

        try {
            // 1. Create the multicast socket and join the mDNS group.
            socket = new MulticastSocket(null);
            socket.setReuseAddress(true);
            try {
                socket.bind(new java.net.InetSocketAddress(5353));
            } catch (java.net.BindException be) {
                // Android's mdnsd may own port 5353 — fall back to an
                // ephemeral port. Queries still reach the multicast group;
                // replies arrive on the ephemeral port instead of 5353.
                socket.bind(new java.net.InetSocketAddress(0));
            }
            socket.setTimeToLive(255);
            socket.setSoTimeout(timeoutMs);
            group = InetAddress.getByName("224.0.0.251");
            socket.joinGroup(group);

            // 2. Send PTR queries for _uscan._tcp and _scanner._tcp.
            sendMdnsQuery(socket, group, "_uscan._tcp.local");
            sendMdnsQuery(socket, group, "_scanner._tcp.local");

            // 3. Listen for replies.
            long endTime = System.currentTimeMillis() + timeoutMs;
            byte[] buffer = new byte[8192];
            while (System.currentTimeMillis() < endTime) {
                DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                try {
                    socket.receive(packet);
                } catch (SocketTimeoutException e) {
                    break; // timeout — stop reading
                }
                String parsed = parseMdnsResponse(packet.getData(), packet.getLength());
                if (parsed != null) {
                    synchronized (results) {
                        results.add(parsed);
                    }
                }
            }
        } catch (Exception e) {
            // swallow — return whatever was collected
        } finally {
            if (socket != null) {
                try {
                    if (group != null) socket.leaveGroup(group);
                    socket.close();
                } catch (Exception ignore) {}
            }
        }

        // 4. Return the results as an array.
        synchronized (results) {
            return results.toArray(new String[0]);
        }
    }

    /**
     * Builds and sends an mDNS PTR query for the given service type.
     */
    private static void sendMdnsQuery(MulticastSocket socket, InetAddress group, String serviceType) throws Exception {
        // DNS header + question.
        ByteBuffer buffer = ByteBuffer.allocate(512).order(ByteOrder.BIG_ENDIAN);

        // DNS header
        buffer.putShort((short) 0); // ID = 0 (mDNS uses 0)
        buffer.putShort((short) 0); // flags = 0 (standard query)
        buffer.putShort((short) 1); // QDCOUNT = 1
        buffer.putShort((short) 0); // ANCOUNT = 0
        buffer.putShort((short) 0); // NSCOUNT = 0
        buffer.putShort((short) 0); // ARCOUNT = 0

        // Question section (QNAME)
        String[] parts = serviceType.split("\\.");
        for (String part : parts) {
            if (part.isEmpty()) continue;
            byte[] bytes = part.getBytes("UTF-8");
            buffer.put((byte) bytes.length);
            buffer.put(bytes);
        }
        buffer.put((byte) 0); // QNAME terminator

        // QTYPE = PTR (12)
        buffer.putShort((short) 12);
        // QCLASS = IN (1), no cache-flush bit
        buffer.putShort((short) 1);

        byte[] queryData = new byte[buffer.position()];
        buffer.flip();
        buffer.get(queryData);

        DatagramPacket packet = new DatagramPacket(queryData, queryData.length, group, 5353);
        socket.send(packet);
    }

    /**
     * Parses an mDNS reply packet and returns "host:port".
     * Only A (IPv4) and SRV (port) records are inspected.
     */
    private static String parseMdnsResponse(byte[] data, int length) {
        try {
            ByteBuffer b = ByteBuffer.wrap(data, 0, length).order(ByteOrder.BIG_ENDIAN);
            if (length < 12) return null;

            int qdCount = b.getShort(4) & 0xFFFF;
            int anCount = b.getShort(6) & 0xFFFF;
            int arCount = b.getShort(10) & 0xFFFF;
            b.position(12);

            // Skip the question section (QNAME + QTYPE + QCLASS).
            for (int i = 0; i < qdCount; i++) {
                skipName(b);
                b.getShort();   // QTYPE
                b.getShort();   // QCLASS
            }

            String host = null;
            int port = -1;

            // Read the answer section.
            for (int i = 0; i < anCount && b.remaining() >= 10; i++) {
                skipName(b);
                int type = b.getShort() & 0xFFFF;
                b.getShort();   // class
                b.getInt();     // ttl
                int rdLength = b.getShort() & 0xFFFF;
                int rdStart = b.position();

                if (type == 1 && rdLength == 4) {           // A (IPv4)
                    byte[] ip = new byte[4];
                    b.get(ip);
                    host = InetAddress.getByAddress(ip).getHostAddress();
                } else if (type == 33 && rdLength >= 6) {   // SRV
                    b.getShort();   // priority
                    b.getShort();   // weight
                    port = b.getShort() & 0xFFFF;
                }

                b.position(rdStart + rdLength);
            }

            // Additional section — SRV and A records are usually here.
            for (int i = 0; i < arCount && b.remaining() >= 10; i++) {
                skipName(b);
                int type = b.getShort() & 0xFFFF;
                b.getShort();   // class
                b.getInt();     // ttl
                int rdLength = b.getShort() & 0xFFFF;
                int rdStart = b.position();

                if (type == 1 && rdLength == 4) {
                    byte[] ip = new byte[4];
                    b.get(ip);
                    host = InetAddress.getByAddress(ip).getHostAddress();
                } else if (type == 33 && rdLength >= 6) {
                    b.getShort();   // priority
                    b.getShort();   // weight
                    port = b.getShort() & 0xFFFF;
                }

                b.position(rdStart + rdLength);
            }

            if (host != null && port > 0) return host + ":" + port;
        } catch (Exception ignore) { }
        return null;
    }

    /**
     * Skips a DNS name (QNAME or NAME), following compression pointers (0xC0).
     */
    private static void skipName(ByteBuffer buffer) {
        while (true) {
            int len = buffer.get() & 0xFF;
            if (len == 0) break;
            if ((len & 0xC0) == 0xC0) { // compression pointer
                buffer.get();           // skip second byte of the pointer
                break;
            }
            buffer.position(buffer.position() + len);
        }
    }
}