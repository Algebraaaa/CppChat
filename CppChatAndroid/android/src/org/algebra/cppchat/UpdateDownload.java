package org.algebra.cppchat;

import android.app.DownloadManager;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONObject;

/** System-owned transfer with a private durable receipt; no Activity or native callbacks retained. */
public final class UpdateDownload {
    private UpdateDownload() {}

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences("update-download", Context.MODE_PRIVATE);
    }

    private static DownloadManager manager(Context context) {
        DownloadManager result = (DownloadManager) context.getSystemService(Context.DOWNLOAD_SERVICE);
        if (result == null) throw new IllegalStateException("系统下载管理器不可用");
        return result;
    }

    private static JSONObject saved(Context context) throws Exception {
        String value = prefs(context).getString("manifest", "");
        return value.isEmpty() ? null : new JSONObject(value);
    }

    private static String error(Exception exception) {
        JSONObject result = new JSONObject();
        try { result.put("error", "更新任务失败：" + exception.getMessage()); }
        catch (Exception ignored) { /* JSONObject string values are supported */ }
        return result.toString();
    }

    private static File file(Context context, JSONObject manifest, String extension) throws Exception {
        String hash = manifest.getString("sha256");
        if (!hash.matches("[0-9a-f]{64}")) throw new SecurityException("无效的安装包标识");
        File base = (extension.equals(".part")
                ? context.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS) : context.getFilesDir());
        if (base == null) throw new IllegalStateException("安装包存储不可用");
        base = base.getCanonicalFile();
        File directory = new File(base, extension.equals(".part") ? "cppchat-updates" : "updates");
        directory = directory.getCanonicalFile();
        if (!base.equals(directory.getParentFile())) throw new SecurityException("安装包目录无效");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("无法创建更新目录");
        File result = new File(directory, hash + extension).getCanonicalFile();
        if (!directory.equals(result.getParentFile())) throw new SecurityException("安装包路径无效");
        return result;
    }

    private static List<Long> matchingTasks(Context context, JSONObject manifest) throws Exception {
        File path = file(context, manifest, ".part");
        List<Long> found = new ArrayList<>();
        try (Cursor cursor = manager(context).query(new DownloadManager.Query())) {
            if (cursor == null) throw new IllegalStateException("无法查询系统下载任务");
            while (cursor.moveToNext()) {
                String local = cursor.getString(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_LOCAL_URI));
                String origin = cursor.getString(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_URI));
                Uri location = local == null ? null : Uri.parse(local);
                boolean sameFile = location != null && "file".equals(location.getScheme())
                        && location.getPath() != null
                        && path.equals(new File(location.getPath()).getCanonicalFile());
                if (sameFile && manifest.getString("downloadUrl").equals(origin)) {
                    found.add(cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_ID)));
                }
            }
        }
        return found;
    }

    // Recover even if the process died between enqueue() and persisting its returned ID.
    private static long findTask(Context context, JSONObject manifest) throws Exception {
        List<Long> tasks = matchingTasks(context, manifest);
        if (tasks.size() > 1) throw new IllegalStateException("存在重复更新任务，请取消后重试");
        long found = tasks.isEmpty() ? -1 : tasks.get(0);
        if (found != -1 && !prefs(context).edit().putLong("id", found).commit())
            throw new IllegalStateException("无法保存更新任务");
        return found;
    }

    private static JSONObject snapshot(Context context, JSONObject manifest) throws Exception {
        JSONObject result = new JSONObject().put("manifest", manifest);
        long id = findTask(context, manifest);
        if (id == -1) {
            boolean ready = file(context, manifest, ".apk").isFile();
            return result.put("status", ready ? DownloadManager.STATUS_SUCCESSFUL : DownloadManager.STATUS_FAILED)
                    .put("received", ready ? manifest.getLong("size") : 0);
        }
        try (Cursor cursor = manager(context).query(new DownloadManager.Query().setFilterById(id))) {
            if (cursor == null || !cursor.moveToFirst()) throw new IllegalStateException("系统下载任务已移除");
            return result.put("status", cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS)))
                    .put("received", cursor.getLong(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_BYTES_DOWNLOADED_SO_FAR)))
                    .put("reason", cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)));
        }
    }

    public static synchronized String inspect(Context context) {
        try {
            JSONObject manifest = saved(context);
            return manifest == null ? "{\"empty\":true}" : snapshot(context, manifest).toString();
        } catch (Exception exception) { return error(exception); }
    }

    private static void discard(Context context) throws Exception {
        JSONObject manifest;
        try {
            manifest = saved(context);
        } catch (org.json.JSONException malformed) {
            // No trusted identity/path can be recovered: clear only this app's receipt.
            if (!prefs(context).edit().clear().commit()) throw new IllegalStateException("无法清理更新记录");
            return;
        }
        if (manifest != null) {
            // An explicit cancel also recovers duplicate tasks, scoped to this exact URL/path.
            List<Long> tasks = matchingTasks(context, manifest);
            for (long id : tasks) manager(context).remove(id);
            for (String extension : new String[] {".part", ".apk", ".tmp"}) {
                File target = file(context, manifest, extension);
                if (target.exists() && !target.delete()) throw new IllegalStateException("无法清理旧安装包");
            }
        }
        if (!prefs(context).edit().clear().commit()) throw new IllegalStateException("无法清理更新记录");
    }

    public static synchronized String cancel(Context context) {
        try { discard(context); return "{\"cancelled\":true}"; }
        catch (Exception exception) { return error(exception); }
    }

    public static synchronized String start(Context context, String value) {
        try {
            JSONObject manifest = new JSONObject(value);
            Uri uri = Uri.parse(manifest.getString("downloadUrl"));
            if (!"https".equals(uri.getScheme()) || uri.getHost() == null || uri.getUserInfo() != null
                    || uri.getQuery() != null || uri.getFragment() != null
                    || !manifest.getString("version").matches("[0-9]{1,5}\\.[0-9]{1,5}\\.[0-9]{1,5}")
                    || manifest.getLong("size") <= 0 || manifest.getLong("size") > 512L * 1024 * 1024)
                throw new SecurityException("更新信息无效");
            file(context, manifest, ".part");
            JSONObject old = saved(context);
            if (old != null && old.getString("sha256").equals(manifest.getString("sha256"))
                    && old.getString("downloadUrl").equals(manifest.getString("downloadUrl"))
                    && old.getLong("size") == manifest.getLong("size")) {
                JSONObject current = snapshot(context, old);
                if (current.getInt("status") != DownloadManager.STATUS_FAILED) return current.toString();
            }
            discard(context);
            if (!prefs(context).edit().putString("manifest", manifest.toString()).putLong("id", -1).commit())
                throw new IllegalStateException("无法保存更新信息");
            DownloadManager.Request request = new DownloadManager.Request(uri)
                    .setTitle("CppChat " + manifest.getString("version"))
                    .setDescription("正在后台下载更新，可锁屏或离开 CppChat")
                    // A system notification must not offer an unverified APK to the installer.
                    .setMimeType("application/octet-stream")
                    .setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE)
                    .setAllowedOverMetered(true).setAllowedOverRoaming(false)
                    .setDestinationInExternalFilesDir(context, Environment.DIRECTORY_DOWNLOADS,
                            "cppchat-updates/" + manifest.getString("sha256") + ".part");
            long id = manager(context).enqueue(request);
            if (!prefs(context).edit().putLong("id", id).commit()) {
                manager(context).remove(id);
                throw new IllegalStateException("无法保存系统下载任务");
            }
            return snapshot(context, manifest).toString();
        } catch (Exception exception) { return error(exception); }
    }

    private static void validatePackage(Context context, File apk, JSONObject manifest) throws Exception {
        PackageManager pm = context.getPackageManager();
        PackageInfo installed = pm.getPackageInfo(context.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES);
        PackageInfo archive = pm.getPackageArchiveInfo(apk.getPath(), PackageManager.GET_SIGNING_CERTIFICATES);
        if (archive == null || !context.getPackageName().equals(archive.packageName)
                || !manifest.getString("version").equals(archive.versionName)
                || archive.getLongVersionCode() <= installed.getLongVersionCode()
                || archive.signingInfo == null || installed.signingInfo == null)
            throw new SecurityException("安装包应用标识或版本不匹配");
        Signature[] expected = installed.signingInfo.getApkContentsSigners();
        Signature[] actual = archive.signingInfo.getApkContentsSigners();
        if (expected.length == 0 || expected.length != actual.length)
            throw new SecurityException("安装包签名不匹配");
        for (Signature signature : expected) {
            if (!Arrays.asList(actual).contains(signature)) throw new SecurityException("安装包签名不匹配");
        }
    }

    public static synchronized String verify(Context context) {
        try {
            JSONObject manifest = saved(context);
            if (manifest == null) throw new IllegalStateException("更新记录已移除");
            File ready = file(context, manifest, ".apk");
            File source = ready.isFile() ? ready : file(context, manifest, ".part");
            File temporary = file(context, manifest, ".tmp");
            if (source.length() != manifest.getLong("size")) throw new SecurityException("安装包大小校验失败");
            MessageDigest hash = MessageDigest.getInstance("SHA-256");
            long total = 0;
            try (FileInputStream input = new FileInputStream(source);
                    FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[256 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    total += count;
                    if (total > manifest.getLong("size")) throw new SecurityException("安装包超出大小限制");
                    hash.update(buffer, 0, count);
                    output.write(buffer, 0, count);
                }
                output.getFD().sync();
            }
            StringBuilder digest = new StringBuilder();
            for (byte b : hash.digest()) digest.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
            if (total != manifest.getLong("size") || !digest.toString().equals(manifest.getString("sha256")))
                throw new SecurityException("安装包 SHA-256 校验失败");
            // Package parsing expects the .apk suffix; atomic rename stays inside app-private files.
            if (!temporary.renameTo(ready)) throw new IllegalStateException("无法保存校验后的安装包");
            validatePackage(context, ready, manifest);
            return new JSONObject().put("verified", true).put("path", ready.getPath()).toString();
        } catch (Exception exception) {
            try { discard(context); } catch (Exception ignored) { /* Keep the original verification failure */ }
            return error(exception);
        }
    }

    public static synchronized String install(Context context) {
        try {
            JSONObject manifest = saved(context);
            if (manifest == null) throw new IllegalStateException("更新记录已移除");
            File apk = file(context, manifest, ".apk");
            validatePackage(context, apk, manifest);
            if (!context.getPackageManager().canRequestPackageInstalls()) {
                context.startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + context.getPackageName())).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
                return new JSONObject().put("message", "请允许 CppChat 安装应用，然后返回点击安装更新").toString();
            }
            Uri content = FileProvider.getUriForFile(context, context.getPackageName() + ".updates", apk);
            context.startActivity(new Intent(Intent.ACTION_VIEW).setDataAndType(content,
                    "application/vnd.android.package-archive").addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION));
            return "{\"launched\":true}";
        } catch (Exception exception) { return error(exception); }
    }
}
