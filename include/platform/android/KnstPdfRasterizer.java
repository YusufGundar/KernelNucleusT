// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

package com.knst.print;

import android.graphics.Bitmap;
import android.graphics.pdf.PdfRenderer;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public final class KnstPdfRasterizer {

    /**
     * Renders a PDF one page at a time to JPEG.
     * Output files: <outDir>/page_001.jpg, page_002.jpg, ...
     *
     * @return absolute paths of the created JPEGs, or null on failure
     */
    public static String[] rasterize(String pdfPath, String outDir,
                                     int targetDpi, int jpegQuality) {
        if (pdfPath == null || outDir == null) return null;

        ParcelFileDescriptor pfd = null;
        PdfRenderer renderer = null;
        try {
            File f = new File(pdfPath);
            if (!f.exists()) return null;

            File od = new File(outDir);
            if (!od.exists() && !od.mkdirs()) return null;

            pfd = ParcelFileDescriptor.open(f, ParcelFileDescriptor.MODE_READ_ONLY);
            renderer = new PdfRenderer(pfd);
            int pageCount = renderer.getPageCount();
            if (pageCount <= 0) return null;

            if (targetDpi <= 0) targetDpi = 300;
            if (jpegQuality <= 0 || jpegQuality > 100) jpegQuality = 85;

            String[] out = new String[pageCount];

            for (int i = 0; i < pageCount; i++) {
                PdfRenderer.Page page = renderer.openPage(i);
                try {
                    // PDF pages are laid out in 72 dpi units — scale up to the target dpi.
                    float scale = targetDpi / 72.0f;
                    int w = (int)(page.getWidth()  * scale);
                    int h = (int)(page.getHeight() * scale);
                    if (w <= 0 || h <= 0) return null;

                    Bitmap bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
                    bmp.eraseColor(android.graphics.Color.WHITE);

                    page.render(bmp, null, null, PdfRenderer.Page.RENDER_MODE_FOR_PRINT);

                    File jpg = new File(od, String.format("page_%03d.jpg", i + 1));
                    try (FileOutputStream fos = new FileOutputStream(jpg)) {
                        bmp.compress(Bitmap.CompressFormat.JPEG, jpegQuality, fos);
                    } finally {
                        bmp.recycle();
                    }
                    out[i] = jpg.getAbsolutePath();
                } finally {
                    page.close();
                }
            }
            return out;
        } catch (Exception e) {
            return null;
        } finally {
            if (renderer != null) {
                try { renderer.close(); } catch (Exception ignored) {}
            }
            if (pfd != null) {
                try { pfd.close(); } catch (Exception ignored) {}
            }
        }
    }
}