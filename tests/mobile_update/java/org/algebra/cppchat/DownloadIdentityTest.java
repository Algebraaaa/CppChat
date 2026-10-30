package org.algebra.cppchat;

import java.io.File;

/** Tests the production policy used with real DownloadManager cursor fields. */
public final class DownloadIdentityTest {
    private static final String URL = "https://updates.example.invalid/app.apk";
    private static final String TITLE = "CppChat 0.1.25";
    private static final String DESCRIPTION = "download update";
    private static int checked;

    private static void check(boolean expected, long id, long saved, String url, String local,
            File destination, String title, String description) {
        boolean actual = DownloadIdentity.belongs(id, saved, url, URL, local, destination,
                title, description, TITLE, DESCRIPTION);
        if (actual != expected) throw new AssertionError("identity case " + checked);
        checked++;
    }

    public static void main(String[] args) throws Exception {
        File directory = new File(args[0]).getCanonicalFile();
        if (!directory.isDirectory() && !directory.mkdirs()) throw new AssertionError("test directory");
        File destination = new File(directory, "package.part").getCanonicalFile();
        check(true, 7, 7, URL, null, destination, null, null); // persisted ID, filename pending
        check(true, 7, 7, URL, "", destination, null, null);
        check(true, 8, -1, URL, null, destination, TITLE, DESCRIPTION); // crash before ID commit
        check(false, 8, -1, URL, null, destination, "another job", DESCRIPTION);
        check(false, 8, -1, URL, null, destination, TITLE, "another description");
        check(false, 7, 7, "https://other.invalid/a", null, destination, TITLE, DESCRIPTION);
        check(false, 7, 7, URL, new File(directory, "other.part").toURI().toString(), destination, TITLE, DESCRIPTION);
        check(true, 7, -1, URL, new File(directory, "./package.part").toURI().toString(), destination, null, null);
        check(false, 7, 7, URL, "content://downloads/7", destination, TITLE, DESCRIPTION);
        check(false, 7, 7, URL, "file://other-host/package.part", destination, TITLE, DESCRIPTION);
        check(false, -1, -1, URL, null, destination, TITLE, DESCRIPTION);
        System.out.println(checked + " production download identity cases PASS");
    }
}
