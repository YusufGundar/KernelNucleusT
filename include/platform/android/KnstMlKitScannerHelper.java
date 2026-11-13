// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


package com.knst.print;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentSender;
import com.google.android.gms.tasks.Task;
import com.google.mlkit.vision.documentscanner.GmsDocumentScanning;
import com.google.mlkit.vision.documentscanner.GmsDocumentScanningResult;
import com.google.mlkit.vision.documentscanner.GmsDocumentScanner;
import com.google.mlkit.vision.documentscanner.GmsDocumentScannerOptions;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;

public final class KnstMlKitScannerHelper {
    public static final int REQUEST_CODE = 0x4B4E;

    private static Activity sActivity;
    private static String sOutPath;
    private static volatile boolean sDone = false;
    private static volatile boolean sOk = false;
    private static volatile int sPages = 0;

    public static void setActivity(Activity a) {
        sActivity = a;
    }

    public static boolean scanToPdf(String outPath, int pageLimit) {
        if (sActivity == null) return false;
        if (outPath == null) return false;

        // Manual init was removed — MlKitContext initializes automatically
        // once firebase-components is on the classpath.

        sOutPath = outPath;
        sDone = false;
        sOk = false;
        sPages = 0;

        // Try FULL mode first, fall back to BASE if it fails.
        boolean ok = tryMode(GmsDocumentScannerOptions.SCANNER_MODE_FULL, pageLimit);
        if (!ok) {
            ok = tryMode(GmsDocumentScannerOptions.SCANNER_MODE_BASE, pageLimit);
        }
        return ok;
    }

    private static boolean tryMode(int scannerMode, int pageLimit) {
        sDone = false;
        sOk = false;

        try {
            GmsDocumentScannerOptions opts = new GmsDocumentScannerOptions.Builder()
                .setGalleryImportAllowed(false)
                .setPageLimit(pageLimit > 0 ? pageLimit : 20)
                .setResultFormats(GmsDocumentScannerOptions.RESULT_FORMAT_PDF)
                .setScannerMode(scannerMode)
                .build();

            GmsDocumentScanner scanner = GmsDocumentScanning.getClient(opts);
            Task<IntentSender> task = scanner.getStartScanIntent(sActivity);

            task.addOnSuccessListener(intentSender -> {
                try {
                    sActivity.startIntentSenderForResult(
                        intentSender, REQUEST_CODE, null, 0, 0, 0);
                } catch (IntentSender.SendIntentException ex) {
                    sDone = true; sOk = false;
                } catch (Exception ex) {
                    sDone = true; sOk = false;
                }
            }).addOnFailureListener(ex -> {
                sDone = true; sOk = false;
            });
        } catch (Throwable ex) {
            sDone = true; sOk = false;
            return false;
        }

        // Wait for the user to finish scanning — up to 120 s.
        long deadline = System.currentTimeMillis() + 120_000L;
        while (!sDone && System.currentTimeMillis() < deadline) {
            try { Thread.sleep(100); } catch (InterruptedException ie) { break; }
        }

        if (!sDone) return false;
        return sOk;
    }

    public static void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQUEST_CODE) return;

        if (resultCode != Activity.RESULT_OK) {
            sDone = true; sOk = false; return;
        }
        if (data == null) {
            sDone = true; sOk = false; return;
        }

        GmsDocumentScanningResult res = GmsDocumentScanningResult.fromActivityResultIntent(data);
        if (res == null) {
            sDone = true; sOk = false; return;
        }
        if (res.getPdf() == null) {
            sDone = true; sOk = false; return;
        }

        try (InputStream in = sActivity.getContentResolver()
                .openInputStream(res.getPdf().getUri());
             FileOutputStream out = new FileOutputStream(new File(sOutPath))) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            sPages = res.getPdf().getPageCount();
            sOk = true;
        } catch (Exception e) {
            sOk = false;
        } finally {
            sDone = true;
        }
    }

    public static int lastPageCount() { return sPages; }
}