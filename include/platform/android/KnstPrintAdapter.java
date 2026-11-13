// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


// include/platform/android/KnstPrintAdapter.java
package com.knst.print;

import android.content.Context;
import android.os.Bundle;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.print.PageRange;
import android.print.PrintAttributes;
import android.print.PrintDocumentAdapter;
import android.print.PrintDocumentInfo;
import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

public class KnstPrintAdapter extends PrintDocumentAdapter {
    private final String pdfPath;
    private final String jobName;
    private PrintAttributes attributes;
    private int pageCount = -1;

    public KnstPrintAdapter(String pdfPath, String jobName) {
        this.pdfPath = pdfPath;
        this.jobName = jobName;
    }

    @Override
    public void onLayout(PrintAttributes oldAttrs, PrintAttributes newAttrs,
                         CancellationSignal cancel, LayoutResultCallback callback, Bundle extras) {
        attributes = newAttrs;
        if (cancel.isCanceled()) { callback.onLayoutCancelled(); return; }

        // Count the PDF pages once — PrintManager cannot render the preview
        // without it and the loading bar gets stuck.
        if (pageCount < 0) pageCount = countPdfPages(pdfPath);

        PrintDocumentInfo.Builder builder = new PrintDocumentInfo.Builder(jobName)
                .setContentType(PrintDocumentInfo.CONTENT_TYPE_DOCUMENT);
        if (pageCount > 0) builder.setPageCount(pageCount);
        PrintDocumentInfo info = builder.build();

        callback.onLayoutFinished(info, !newAttrs.equals(oldAttrs));
    }

    @Override
    public void onWrite(PageRange[] pages, ParcelFileDescriptor dest,
                        CancellationSignal cancel, WriteResultCallback callback) {
        try (FileInputStream in = new FileInputStream(pdfPath);
             FileOutputStream out = new FileOutputStream(dest.getFileDescriptor())) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                if (cancel.isCanceled()) { callback.onWriteCancelled(); return; }
                out.write(buf, 0, n);
            }
            // Report ALL_PAGES — PrintManager honours the page count set via
            // setPageCount and accepts page-range subsets on top of it.
            callback.onWriteFinished(new PageRange[]{PageRange.ALL_PAGES});
        } catch (IOException e) {
            callback.onWriteFailed(e.getMessage());
        }
    }

    // Counts "/Type /Page" occurrences (the ones not followed by 's') in
    // the raw PDF bytes. FlateDecode-compressed page dictionaries are not
    // counted, but most modern generators leave them uncompressed.
    // Falls back to 1 when nothing is found.
    private static int countPdfPages(String path) {
        try (FileInputStream in = new FileInputStream(path)) {
            ByteArrayOutputStream acc = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            // First 8 MB is enough (a 3-page PDF is ~50 KB).
            while ((n = in.read(buf)) > 0 && acc.size() < 8 * 1024 * 1024) {
                acc.write(buf, 0, n);
            }
            String content = new String(acc.toByteArray(), "ISO-8859-1");
            int count = 0, pos = 0;
            while ((pos = content.indexOf("/Type", pos)) >= 0) {
                pos += 5;
                while (pos < content.length() && Character.isWhitespace(content.charAt(pos))) pos++;
                if (pos + 5 <= content.length()
                        && content.regionMatches(pos, "/Page", 0, 5)
                        && (pos + 5 >= content.length()
                            || content.charAt(pos + 5) != 's')) {
                    count++;
                    pos += 5;
                }
            }
            return count > 0 ? count : 1;
        } catch (Exception e) {
            return 1;
        }
    }
}