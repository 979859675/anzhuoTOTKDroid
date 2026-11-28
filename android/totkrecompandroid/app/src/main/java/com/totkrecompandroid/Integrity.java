package com.totkrecompandroid;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.content.pm.SigningInfo;
import android.os.Debug;

import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.security.MessageDigest;

/** The anti-tamper check of the release build: the app must be a release build, with no debugger or tracer attached,
 *  signed with the certificate it was built with (BuildConfig.EXPECTED_CERT_SHA256, from build.gradle.kts). A check on
 *  the phone: it stops the usual repackaging and debugging, not a determined person with time. Debug builds skip it. */
final class Integrity {
    private Integrity() {}

    /** null when the app passes; otherwise a short code for the user to report. */
    static String problem(Context c) {
        if (BuildConfig.DEBUG) return null;
        if ((c.getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0) return "debuggable";
        if (Debug.isDebuggerConnected()) return "debugger";
        if (tracerAttached()) return "tracer";
        String cert = signerSha256(c);
        if (!BuildConfig.EXPECTED_CERT_SHA256.equalsIgnoreCase(cert)) return "signature";
        return null;
    }

    // /proc/self/status: TracerPid is not 0 while something (gdb, strace, ptrace) is attached to this process
    private static boolean tracerAttached() {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/self/status"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.startsWith("TracerPid:")) return Integer.parseInt(line.substring(10).trim()) != 0;
            }
        } catch (IOException | NumberFormatException e) {
            // (cannot read it: no evidence of a tracer)
        }
        return false;
    }

    // SHA-256 of the certificate that signed the installed APK (the same bytes the build hashed); "" if unknown
    private static String signerSha256(Context c) {
        try {
            PackageManager pm = c.getPackageManager();
            SigningInfo info = pm.getPackageInfo(c.getPackageName(), PackageManager.GET_SIGNING_CERTIFICATES).signingInfo;
            if (info == null) return "";
            Signature[] signers = info.getApkContentsSigners();
            if (signers == null || signers.length == 0) return "";
            byte[] d = MessageDigest.getInstance("SHA-256").digest(signers[0].toByteArray());
            StringBuilder sb = new StringBuilder();
            for (byte b : d) sb.append(String.format("%02x", b & 0xff));
            return sb.toString();
        } catch (Exception e) {
            return "";
        }
    }
}
