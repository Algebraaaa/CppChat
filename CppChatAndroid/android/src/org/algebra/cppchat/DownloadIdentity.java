package org.algebra.cppchat;

import java.io.File;
import java.net.URI;
import java.util.Objects;

/** Pure identity policy, also exercised without an Android runtime. */
final class DownloadIdentity {
    private DownloadIdentity() {}

    static boolean belongs(long rowId, long savedId, String url, String expectedUrl,
            String localUri, File expectedFile, String title, String description,
            String expectedTitle, String expectedDescription) {
        if (rowId <= 0 || !Objects.equals(url, expectedUrl)) return false;
        if (localUri == null || localUri.isEmpty()) {
            // DownloadProvider has not allocated a filename yet. A persisted ID is authoritative;
            // the enqueue/receipt crash window falls back to our immutable URL + request metadata.
            return rowId == savedId || (expectedTitle != null && !expectedTitle.isEmpty()
                    && expectedDescription != null && !expectedDescription.isEmpty()
                    && Objects.equals(title, expectedTitle)
                    && Objects.equals(description, expectedDescription));
        }
        try {
            URI location = new URI(localUri);
            return "file".equals(location.getScheme()) && location.getAuthority() == null
                    && location.getPath() != null
                    && expectedFile.getCanonicalFile().equals(new File(location.getPath()).getCanonicalFile());
        } catch (Exception invalidPath) {
            return false;
        }
    }
}
