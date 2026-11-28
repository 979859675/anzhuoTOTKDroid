package com.totkrecompandroid;

import android.content.Context;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;

import javax.crypto.Cipher;
import javax.crypto.CipherInputStream;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** The game's native library (libmain.so), encrypted at build time when the build has the key (build.gradle.kts,
 *  encryptMainLib): assets/libmain.bin = "TLB1" + a 12-byte IV + AES-256-GCM of the library. The key comes from the
 *  account server with a valid session, at the launch (LoginActivity.enterGame), and is kept only in memory until the
 *  library is loaded. The library is decrypted into the app's cache folder and loaded from there (SDL loads it by that
 *  path too). A build without the key has the plain library, and nothing here applies. */
final class LibKey {
    static final String ASSET = "libmain.bin";
    private static final byte[] MAGIC = {'T', 'L', 'B', '1'};

    private static byte[] key;             // 32 bytes, in memory only
    private static volatile String loadedPath;

    private LibKey() {}

    /** True when this build has the library encrypted (the asset is there). */
    static boolean encrypted(Context c) {
        try (InputStream in = c.getAssets().open(ASSET)) {
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /** Keeps the key from the server's answer (64 hex characters); false if it is not one. */
    static synchronized boolean set(String hex) {
        if (hex == null || !hex.matches("[0-9a-fA-F]{64}")) return false;
        byte[] k = new byte[32];
        for (int i = 0; i < 32; i++) k[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        forget();
        key = k;
        return true;
    }

    static synchronized void forget() {
        if (key != null) Arrays.fill(key, (byte) 0);
        key = null;
    }

    /** Decrypts the library into the cache folder and returns its file. Needs the key from set(). */
    static synchronized File decryptTo(Context c) throws IOException {
        if (key == null) throw new IOException("the game's key is missing: start the game from the login page");
        File out = new File(c.getCodeCacheDir(), "libmain.so");
        try (InputStream raw = new BufferedInputStream(c.getAssets().open(ASSET))) {
            byte[] head = readFully(raw, 16);
            if (!Arrays.equals(Arrays.copyOf(head, 4), MAGIC)) throw new IOException("the game's library file is not valid");
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"),
                    new GCMParameterSpec(128, Arrays.copyOfRange(head, 4, 16)));
            try (InputStream in = new CipherInputStream(raw, cipher); OutputStream o = new FileOutputStream(out)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) o.write(buf, 0, n);  // (GCM checks the tag at the end: a wrong key fails here)
            }
        } catch (IOException | RuntimeException e) {
            out.delete();
            throw e;
        } catch (Exception e) {
            out.delete();
            throw new IOException("cannot decrypt the game's library: " + e.getClass().getSimpleName());
        }
        out.setReadable(true, true);
        out.setExecutable(true, true);
        return out;
    }

    static void setLoadedPath(String path) { loadedPath = path; }

    /** The decrypted library's path once it is loaded (null for a plain build). */
    static String loadedPath() { return loadedPath; }

    private static byte[] readFully(InputStream in, int n) throws IOException {
        byte[] b = new byte[n];
        int off = 0;
        while (off < n) {
            int r = in.read(b, off, n - off);
            if (r < 0) throw new IOException("the game's library file is too short");
            off += r;
        }
        return b;
    }
}
