package com.totkrecompandroid;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;

/** The first screen of the app (the launcher, and the target of the shortcuts): landscape, the game's background and a
 *  login window in the middle. It checks the login with the account server, and only then starts the game
 *  (TOTKRecompAndroidActivity, not exported). Without a valid login - or without the network to check it - the login
 *  window stays. */
public class LoginActivity extends Activity {
    // the start screen's look: gold on the dark sky
    private static final int GOLD = 0xFFD9B26A, PANEL = 0xE6101418, FIELD = 0xFF1E2227, EDGE = 0xFF4A5159,
            MUTED = 0xFFB8BEC6, WARN = 0xFFFFAA6E, INK = 0xFF101418;
    private static final int MATCH = ViewGroup.LayoutParams.MATCH_PARENT, WRAP = ViewGroup.LayoutParams.WRAP_CONTENT;

    private final Handler ui = new Handler(Looper.getMainLooper());
    private EditText user, pass, phrase;
    private TextView status;
    private Button primary, toggle;
    private boolean registerMode, busy;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        String problem = Integrity.problem(this);
        if (problem != null) {  // not the app that was built (Integrity.java): no login, no game
            setContentView(blockedScreen(problem));
            return;
        }
        setContentView(buildScreen());
        if (Auth.token(this) != null) {
            setBusy(true, "正在校验登录…");
            new Thread(() -> {
                Auth.Result r = Auth.check(this);
                ui.post(() -> {
                    if (r.ok()) {
                        startGame();
                        return;
                    }
                    setBusy(false, "");
                    if (r.code == 401 || r.code == 403) {  // the session ended, or the account is off
                        Auth.clearSession(this);
                        showStatus(r.code == 403 ? "账号已被禁用，请联系管理员" : "登录已过期，请重新登录");
                    } else {
                        showStatus("无法连接登录服务器，请检查网络后重新打开（需要联网登录）");
                    }
                });
            }).start();
        }
    }

    private View blockedScreen(String code) {
        TextView t = text("应用校验未通过，无法启动。\n请从官方渠道重新安装。\n（代码 " + code + "）", 16, WARN, false);
        t.setBackgroundColor(Color.BLACK);
        t.setPadding(dp(24), dp(24), dp(24), dp(24));
        return t;
    }

    // The game's background (assets/res, from the repository's artwork) under a dark shade, the window in the middle.
    // In a ScrollView, so the window scrolls up when the keyboard covers it.
    private View buildScreen() {
        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        Bitmap art = background();
        if (art != null) {
            ImageView bg = new ImageView(this);
            bg.setImageBitmap(art);
            bg.setScaleType(ImageView.ScaleType.CENTER_CROP);
            root.addView(bg, new FrameLayout.LayoutParams(MATCH, MATCH));
        }
        View shade = new View(this);
        shade.setBackgroundColor(0x99000000);
        root.addView(shade, new FrameLayout.LayoutParams(MATCH, MATCH));

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER);
        column.setPadding(dp(16), dp(12), dp(16), dp(12));
        column.addView(panel(), new LinearLayout.LayoutParams(panelWidth(), WRAP));
        scroll.addView(column, new FrameLayout.LayoutParams(MATCH, MATCH));
        root.addView(scroll, new FrameLayout.LayoutParams(MATCH, MATCH));
        return root;
    }

    private LinearLayout panel() {
        LinearLayout p = new LinearLayout(this);
        p.setOrientation(LinearLayout.VERTICAL);
        p.setGravity(Gravity.CENTER_HORIZONTAL);
        p.setPadding(dp(24), dp(18), dp(24), dp(20));
        p.setBackground(box(PANEL, GOLD, 14));

        p.addView(text("TOTKDroid", 26, GOLD, true), margin(0));
        p.addView(text("账号登录", 15, MUTED, false), margin(2));
        p.addView(text("QQ 群：" + Auth.GROUP, 13, MUTED, false), margin(2));

        user = field("用户名（字母、数字、下划线，3-20 位）", false);
        pass = field("密码（6-72 位）", true);
        phrase = field("注册口令（由管理员提供）", false);
        phrase.setVisibility(View.GONE);
        p.addView(user, margin(14));
        p.addView(pass, margin(10));
        p.addView(phrase, margin(10));

        status = text("", 13, WARN, false);
        p.addView(status, margin(8));

        primary = new Button(this);
        primary.setText("登录");
        primary.setAllCaps(false);
        primary.setTextSize(16);
        primary.setTextColor(INK);
        primary.setBackground(box(GOLD, GOLD, 10));
        primary.setOnClickListener(v -> submit());
        p.addView(primary, margin(10));

        toggle = new Button(this);
        toggle.setText("没有账号？注册");
        toggle.setAllCaps(false);
        toggle.setTextSize(14);
        toggle.setTextColor(GOLD);
        toggle.setBackground(box(0x00000000, 0x00000000, 10));
        toggle.setOnClickListener(v -> {
            registerMode = !registerMode;
            phrase.setVisibility(registerMode ? View.VISIBLE : View.GONE);
            primary.setText(registerMode ? "注册" : "登录");
            toggle.setText(registerMode ? "已有账号？登录" : "没有账号？注册");
            showStatus("");
        });
        p.addView(toggle, margin(4));
        return p;
    }

    private void submit() {
        if (busy) return;
        String u = user.getText().toString().trim(), p = pass.getText().toString();
        if (u.isEmpty() || p.isEmpty()) {
            showStatus("请填写用户名和密码");
            return;
        }
        if (registerMode) {
            String ph = phrase.getText().toString().trim();
            if (ph.isEmpty()) {
                showStatus("请填写注册口令");
                return;
            }
            setBusy(true, "正在注册…");
            new Thread(() -> {
                Auth.Result r = Auth.register(u, p, ph);
                ui.post(() -> {
                    setBusy(false, "");
                    if (r.ok()) {
                        registerMode = false;
                        phrase.setVisibility(View.GONE);
                        primary.setText("登录");
                        toggle.setText("没有账号？注册");
                        pass.setText("");
                        showStatus("注册成功，请登录");
                    } else {
                        showStatus(registerError(r));
                    }
                });
            }).start();
        } else {
            setBusy(true, "正在登录…");
            new Thread(() -> {
                Auth.Result r = Auth.login(this, u, p);
                ui.post(() -> {
                    if (r.ok()) {
                        startGame();
                        return;
                    }
                    setBusy(false, "");
                    showStatus(loginError(r));
                });
            }).start();
        }
    }

    private void startGame() {
        // the game's own activity (not exported) with what this one got: the shortcut's action, the launch extras
        Intent game = new Intent(this, TOTKRecompAndroidActivity.class);
        game.setAction(getIntent().getAction());
        game.putExtras(getIntent());
        startActivity(game);
        finish();
    }

    private static String loginError(Auth.Result r) {
        if (r.code == -1) return "无法连接登录服务器，请检查网络";
        if (r.code == 401) return "用户名或密码错误";
        if (r.code == 403 && "disabled".equals(r.error())) return "账号已被禁用，请联系管理员";
        if (r.code == 403 && "device limit".equals(r.error())) return "这个账号已绑定 2 台设备，请联系管理员清空设备";
        if (r.code == 400) return "用户名或密码格式不对";
        return "登录失败（" + r.code + "）";
    }

    private static String registerError(Auth.Result r) {
        if (r.code == -1) return "无法连接登录服务器，请检查网络";
        if (r.code == 401) return "注册口令错误";
        if (r.code == 503) return "注册暂未开放";
        if (r.code == 409) return "这个用户名已被占用";
        if (r.code == 400) return "用户名需 3-20 位（字母、数字、下划线），密码需 6-72 位";
        return "注册失败（" + r.code + "）";
    }

    private void setBusy(boolean b, String message) {
        busy = b;
        primary.setEnabled(!b);
        toggle.setEnabled(!b);
        showStatus(message);
    }

    private void showStatus(String s) {
        status.setText(s);
    }

    private Bitmap background() {
        try (InputStream in = getAssets().open("res/launcher-background.png")) {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inSampleSize = 2;  // (a login screen does not need the full picture in memory)
            return BitmapFactory.decodeStream(in, null, o);
        } catch (IOException e) {
            return null;  // (no picture: the black background)
        }
    }

    // the window's width: 400 dp, or the screen less a margin on each side
    private int panelWidth() {
        int screen = getResources().getDisplayMetrics().widthPixels;
        return Math.min(dp(400), screen - dp(32));
    }

    private GradientDrawable box(int fill, int stroke, int radiusDp) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(radiusDp));
        g.setStroke(dp(1), stroke);
        return g;
    }

    private TextView text(String s, int sp, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        if (bold) t.setTypeface(null, Typeface.BOLD);
        return t;
    }

    private EditText field(String hint, boolean password) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setSingleLine(true);
        e.setTextSize(16);
        e.setTextColor(Color.WHITE);
        e.setHintTextColor(0xFF7D8590);
        e.setBackground(box(FIELD, EDGE, 10));
        e.setPadding(dp(12), dp(10), dp(12), dp(10));
        e.setInputType(password ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD
                                : InputType.TYPE_CLASS_TEXT);
        return e;
    }

    // a full-width child with a top margin of v dp
    private LinearLayout.LayoutParams margin(int v) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(MATCH, WRAP);
        lp.topMargin = dp(v);
        return lp;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
