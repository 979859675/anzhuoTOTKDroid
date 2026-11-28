package com.totkrecompandroid;

import android.content.Context;
import android.content.SharedPreferences;
import android.provider.Settings;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.UUID;

/** The account server (ly.xzcqgzs.cn, server/api.php): login, and the check at every launch. Network calls: never on the
 *  main thread. */
final class Auth {
    static final String SERVER = "https://ly.xzcqgzs.cn/api.php?action=";
    static final String GROUP = "1135442077";
    private static final String PREFS = "auth";

    /** An answer from the server; code -1 when there was no answer (no network, timeout). */
    static final class Result {
        final int code;
        final JSONObject body;

        Result(int code, JSONObject body) {
            this.code = code;
            this.body = body;
        }

        boolean ok() { return code == 200; }

        String error() { return body == null ? "" : body.optString("error", ""); }
    }

    private Auth() {}

    /** This phone's id: a random UUID with the Android ID, hashed (64 hex characters; the server takes 16-64). */
    static String deviceId(Context c) {
        SharedPreferences p = c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String id = p.getString("device", null);
        if (id == null) {
            String androidId = Settings.Secure.getString(c.getContentResolver(), Settings.Secure.ANDROID_ID);
            id = sha256(String.valueOf(androidId) + ":" + UUID.randomUUID());
            p.edit().putString("device", id).apply();
        }
        return id;
    }

    static String token(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("token", null);
    }

    static String username(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("username", "");
    }

    static void saveSession(Context c, String user, String token) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString("username", user).putString("token", token).apply();
    }

    static void clearSession(Context c) {
        c.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove("token").remove("username").apply();
    }

    /** Register with the registration passphrase the server admin set. */
    static Result register(String user, String password, String passphrase) {
        return post("register", obj("username", user, "password", password, "passphrase", passphrase));
    }

    /** Log in on this phone; on success the session is kept for the next launches. */
    static Result login(Context c, String user, String password) {
        Result r = post("login", obj("username", user, "password", password, "device_id", deviceId(c)));
        if (r.ok()) saveSession(c, user, r.body.optString("token", ""));
        return r;
    }

    /** The launch check: is this phone's session still valid on the server (needs the network). */
    static Result check(Context c) {
        String token = token(c);
        if (token == null) return new Result(401, null);
        return post("check", obj("device_id", deviceId(c), "token", token));
    }

    /** The game library's key, with a valid session (LibKey.java); the answer's "key" is 64 hex characters. */
    static Result libKey(Context c) {
        String token = token(c);
        if (token == null) return new Result(401, null);
        return post("libkey", obj("device_id", deviceId(c), "token", token));
    }

    private static JSONObject obj(String... kv) {
        JSONObject o = new JSONObject();
        try {
            for (int i = 0; i + 1 < kv.length; i += 2) o.put(kv[i], kv[i + 1]);
        } catch (JSONException e) {
            // (string values only: cannot happen)
        }
        return o;
    }

    private static Result post(String action, JSONObject body) {
        HttpURLConnection conn = null;
        try {
            conn = (HttpURLConnection) new URL(SERVER + action).openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(8000);
            conn.setReadTimeout(10000);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            byte[] data = body.toString().getBytes(StandardCharsets.UTF_8);
            try (OutputStream out = conn.getOutputStream()) {
                out.write(data);
            }
            int code = conn.getResponseCode();
            InputStream in = code >= 400 ? conn.getErrorStream() : conn.getInputStream();
            String text = in == null ? "" : readText(in);
            JSONObject json = text.startsWith("{") ? new JSONObject(text) : new JSONObject();
            return new Result(code, json);
        } catch (IOException | JSONException e) {
            return new Result(-1, null);
        } finally {
            if (conn != null) conn.disconnect();
        }
    }

    private static String readText(InputStream in) throws IOException {
        try (InputStream src = in) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buf = new byte[1024];
            int n;
            while (bytes.size() < 8192 && (n = src.read(buf)) > 0) bytes.write(buf, 0, n);
            return bytes.toString(StandardCharsets.UTF_8.name());
        }
    }

    private static String sha256(String s) {
        try {
            byte[] d = MessageDigest.getInstance("SHA-256").digest(s.getBytes(StandardCharsets.UTF_8));
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format("%02x", b & 0xff));
            return sb.toString();
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);  // (every Android has SHA-256)
        }
    }
}
