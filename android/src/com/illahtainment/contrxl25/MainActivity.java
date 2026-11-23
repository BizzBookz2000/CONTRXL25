package com.illahtainment.contrxl25;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Base64;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

/**
 * CONTRXL 25 for Android: the CONTRXL 25 web app (assets/www/index.html) in a full-screen WebView.
 *
 * The page is served from https://appassets.androidplatform.net/ (a secure origin, so the microphone,
 * Web Audio and storage behave as in Chrome). This activity adds what a WebView lacks:
 *  - microphone permission for Sample
 *  - the system file picker for Load / + Samples / Load song / library import
 *  - saving .wav and song .zip files into Downloads/CONTRXL 25
 *  - USB / Bluetooth-paired MIDI keyboards through android.media.midi (see MidiBridge)
 */
public class MainActivity extends Activity {
    static final String HOST = "appassets.androidplatform.net";
    static final String START = "https://" + HOST + "/index.html";
    private static final int REQ_FILES = 1, REQ_MIC = 2, REQ_STORAGE = 3;

    WebView web;
    private ValueCallback<Uri[]> fileCallback;
    private PermissionRequest pendingMic;
    private MidiBridge midi;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);   // don't dim while making music
        web = new WebView(this);
        web.setBackgroundColor(0xff0d0d10);
        setContentView(web);
        hideSystemBars();

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);                    // localStorage: settings, MIDI links
        s.setDatabaseEnabled(true);                      // IndexedDB: the sample library
        s.setMediaPlaybackRequiresUserGesture(false);    // audio starts with the first touch on a pad
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);                   // samples picked with the file picker
        s.setSupportZoom(true);
        s.setBuiltInZoomControls(true);                  // pinch to zoom on small phones
        s.setDisplayZoomControls(false);
        s.setTextZoom(100);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        WebView.setWebContentsDebuggingEnabled((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0);

        web.addJavascriptInterface(new Bridge(), "CXAndroid");
        midi = new MidiBridge(this);
        web.addJavascriptInterface(midi, "CXMidi");

        web.setWebViewClient(new WebViewClient() {
            @Override public WebResourceResponse shouldInterceptRequest(WebView v, WebResourceRequest r) {
                Uri u = r.getUrl();
                if (!HOST.equals(u.getHost())) return null;
                String path = u.getPath();
                if (path == null || path.equals("/")) path = "/index.html";
                try {
                    InputStream in = getAssets().open("www" + path);
                    WebResourceResponse res = new WebResourceResponse(mimeFor(path), "utf-8", in);
                    Map<String, String> h = new HashMap<>();
                    h.put("Cache-Control", "no-cache");
                    res.setResponseHeaders(h);
                    return res;
                } catch (IOException e) {
                    return new WebResourceResponse("text/plain", "utf-8", 404, "Not found", null, null);
                }
            }
            @SuppressWarnings("deprecation")
            @Override public boolean shouldOverrideUrlLoading(WebView v, String url) {   // (WebView, String): works on every Android version
                Uri u = Uri.parse(url);
                if (HOST.equals(u.getHost())) return false;
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (ActivityNotFoundException ignored) {}
                return true;                             // web links open in the browser, not in the app
            }
        });

        web.setWebChromeClient(new WebChromeClient() {
            @Override public void onPermissionRequest(final PermissionRequest req) {
                runOnUiThread(new Runnable() { @Override public void run() {
                    boolean wantsMic = false;
                    for (String res : req.getResources())
                        if (PermissionRequest.RESOURCE_AUDIO_CAPTURE.equals(res)) wantsMic = true;
                    if (!wantsMic) { req.deny(); return; }
                    if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                        req.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
                    } else {
                        pendingMic = req;
                        requestPermissions(new String[] { Manifest.permission.RECORD_AUDIO }, REQ_MIC);
                    }
                }});
            }
            @Override public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (fileCallback != null) fileCallback.onReceiveValue(null);
                fileCallback = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                String[] accept = p.getAcceptTypes();
                boolean zip = false, audio = false;
                for (String a : accept) {
                    if (a == null) continue;
                    if (a.contains("zip")) zip = true;
                    if (a.startsWith("audio")) audio = true;
                }
                if (audio && !zip) {
                    i.setType("audio/*");
                } else if (zip && !audio) {
                    i.setType("*/*");
                    i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "application/zip", "application/x-zip-compressed", "application/octet-stream" });
                } else {
                    i.setType("*/*");
                }
                if (p.getMode() == FileChooserParams.MODE_OPEN_MULTIPLE) i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(Intent.createChooser(i, "Choose " + (zip && !audio ? "a song (.zip)" : "audio files")), REQ_FILES);
                } catch (ActivityNotFoundException e) {
                    fileCallback = null;
                    return false;
                }
                return true;
            }
        });

        if (saved != null) web.restoreState(saved);
        else web.loadUrl(START);
    }

    static String mimeFor(String path) {
        String p = path.toLowerCase();
        if (p.endsWith(".html")) return "text/html";
        if (p.endsWith(".js")) return "application/javascript";
        if (p.endsWith(".css")) return "text/css";
        if (p.endsWith(".png")) return "image/png";
        if (p.endsWith(".svg")) return "image/svg+xml";
        if (p.endsWith(".pdf")) return "application/pdf";
        if (p.endsWith(".wav")) return "audio/wav";
        return "application/octet-stream";
    }

    private void hideSystemBars() {
        web.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    @Override protected void onActivityResult(int req, int result, Intent data) {
        if (req != REQ_FILES) { super.onActivityResult(req, result, data); return; }
        if (fileCallback == null) return;
        Uri[] uris = null;
        if (result == RESULT_OK && data != null) {
            ClipData clip = data.getClipData();
            if (clip != null && clip.getItemCount() > 0) {
                uris = new Uri[clip.getItemCount()];
                for (int k = 0; k < uris.length; ++k) uris[k] = clip.getItemAt(k).getUri();
            } else if (data.getData() != null) {
                uris = new Uri[] { data.getData() };
            }
        }
        fileCallback.onReceiveValue(uris);
        fileCallback = null;
    }

    @Override public void onRequestPermissionsResult(int req, String[] perms, int[] res) {
        boolean ok = res.length > 0 && res[0] == PackageManager.PERMISSION_GRANTED;
        if (req == REQ_MIC && pendingMic != null) {
            if (ok) pendingMic.grant(new String[] { PermissionRequest.RESOURCE_AUDIO_CAPTURE });
            else {
                pendingMic.deny();
                Toast.makeText(this, "Sample needs the microphone. Allow it in Settings > Apps > CONTRXL 25 > Permissions.", Toast.LENGTH_LONG).show();
            }
            pendingMic = null;
        }
        if (req == REQ_STORAGE && !ok)
            Toast.makeText(this, "Saving to Downloads needs the storage permission.", Toast.LENGTH_LONG).show();
    }

    @Override public void onBackPressed() {
        new AlertDialog.Builder(this)
            .setTitle("Leave CONTRXL 25?")
            .setMessage("Save your song first (Song & Export > Save song) if you want to keep it.")
            .setPositiveButton("Leave", new android.content.DialogInterface.OnClickListener() { @Override public void onClick(android.content.DialogInterface d, int w) { finish(); } })
            .setNegativeButton("Stay", null)
            .show();
    }

    @Override protected void onSaveInstanceState(Bundle out) { super.onSaveInstanceState(out); web.saveState(out); }
    @Override protected void onResume() { super.onResume(); web.onResume(); midi.start(); }
    @Override protected void onDestroy() { midi.stop(); web.destroy(); super.onDestroy(); }

    // ---------------------------------------------------------------- saving files
    /** Called from android-shim.js in pieces (big songs can be tens of MB). */
    class Bridge {
        private ByteArrayList buf;
        private String name = "CONTRXL25", mime = "application/octet-stream";

        @JavascriptInterface public void saveBegin(String fileName, String mimeType) {
            buf = new ByteArrayList();
            name = sanitize(fileName);
            mime = mimeType == null || mimeType.isEmpty() ? guessMime(name) : mimeType;
        }
        @JavascriptInterface public void saveChunk(String base64) { if (buf != null) buf.add(Base64.decode(base64, Base64.DEFAULT)); }
        @JavascriptInterface public String saveEnd() {
            if (buf == null) return "error:nothing to save";
            byte[] data = buf.toBytes();
            buf = null;
            try {
                final String where = writeToDownloads(name, mime, data);
                runOnUiThread(new Runnable() { @Override public void run() { Toast.makeText(MainActivity.this, "Saved " + where, Toast.LENGTH_LONG).show(); } });
                return "ok:" + where;
            } catch (Exception e) {
                final String why = e.getMessage(), nm = name;
                runOnUiThread(new Runnable() { @Override public void run() { Toast.makeText(MainActivity.this, "Could not save " + nm + ": " + why, Toast.LENGTH_LONG).show(); } });
                return "error:" + e.getMessage();
            }
        }
        @JavascriptInterface public String platform() { return "android"; }
    }

    static String sanitize(String n) {
        if (n == null || n.trim().isEmpty()) n = "CONTRXL25";
        return n.replaceAll("[\\\\/:*?\"<>|]", "_");
    }
    static String guessMime(String n) {
        String l = n.toLowerCase();
        if (l.endsWith(".wav")) return "audio/wav";
        if (l.endsWith(".zip")) return "application/zip";
        if (l.endsWith(".mid")) return "audio/midi";
        if (l.endsWith(".json")) return "application/json";
        return "application/octet-stream";
    }

    /** Downloads/CONTRXL 25/<name>. MediaStore on Android 10+, the public folder on 7 to 9. */
    String writeToDownloads(String fileName, String mime, byte[] data) throws IOException {
        if (Build.VERSION.SDK_INT >= 29) {
            ContentResolver cr = getContentResolver();
            ContentValues v = new ContentValues();
            v.put("_display_name", fileName);
            v.put("mime_type", mime);
            v.put("relative_path", "Download/CONTRXL 25");
            v.put("is_pending", 1);
            Uri uri = cr.insert(Uri.parse("content://media/external/downloads"), v);   // MediaStore.Downloads
            if (uri == null) throw new IOException("Downloads is not available");
            try (OutputStream o = cr.openOutputStream(uri)) {
                if (o == null) throw new IOException("cannot write");
                o.write(data);
            }
            ContentValues done = new ContentValues();
            done.put("is_pending", 0);
            cr.update(uri, done, null, null);
            return "Download/CONTRXL 25/" + fileName;
        }
        if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            runOnUiThread(new Runnable() { @Override public void run() { requestPermissions(new String[] { Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQ_STORAGE); } });
            throw new IOException("allow storage access, then save again");
        }
        File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "CONTRXL 25");
        if (!dir.exists() && !dir.mkdirs()) throw new IOException("cannot make " + dir);
        File f = new File(dir, fileName);
        for (int k = 2; f.exists(); ++k) {
            int dot = fileName.lastIndexOf('.');
            f = new File(dir, dot > 0 ? fileName.substring(0, dot) + " (" + k + ")" + fileName.substring(dot) : fileName + " (" + k + ")");
        }
        try (FileOutputStream o = new FileOutputStream(f)) { o.write(data); }
        return "Download/CONTRXL 25/" + f.getName();
    }

    /** Growable byte buffer (chunks arrive one by one). */
    static class ByteArrayList {
        private final ArrayList<byte[]> parts = new ArrayList<>();
        private int size;
        void add(byte[] b) { parts.add(b); size += b.length; }
        byte[] toBytes() {
            byte[] out = new byte[size];
            int p = 0;
            for (byte[] b : parts) { System.arraycopy(b, 0, out, p, b.length); p += b.length; }
            return out;
        }
    }
}
