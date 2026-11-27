package com.totkrecompandroid;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** The first screen of the app (the launcher, and the target of the shortcuts): it checks the login with the account
 *  server, and only then starts the game (TOTKRecompAndroidActivity, not exported). Without a valid login - or without
 *  the network to check it - the login form is shown. */
public class LoginActivity extends Activity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private EditText user, pass, phrase;
    private TextView status;
    private Button primary, toggle;
    private boolean registerMode, busy;
    private LinearLayout form;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        buildForm();
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

    private void buildForm() {
        ScrollView scroll = new ScrollView(this);
        form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setGravity(Gravity.CENTER_HORIZONTAL);
        int pad = dp(32);
        form.setPadding(pad, pad, pad, pad);
        form.setBackgroundColor(Color.BLACK);

        form.addView(text("TOTKDroid 登录", 22, Color.WHITE));
        form.addView(text("QQ 群：" + Auth.GROUP, 15, Color.LTGRAY));

        user = field("用户名（字母、数字、下划线，3-20 位）", false);
        pass = field("密码（6-72 位）", true);
        phrase = field("注册口令（由管理员提供）", false);
        phrase.setVisibility(View.GONE);
        form.addView(user);
        form.addView(pass);
        form.addView(phrase);

        status = text("", 14, Color.rgb(255, 170, 110));
        form.addView(status);

        primary = new Button(this);
        primary.setText("登录");
        primary.setOnClickListener(v -> submit());
        form.addView(primary);

        toggle = new Button(this);
        toggle.setText("没有账号？注册");
        toggle.setOnClickListener(v -> {
            registerMode = !registerMode;
            phrase.setVisibility(registerMode ? View.VISIBLE : View.GONE);
            primary.setText(registerMode ? "注册" : "登录");
            toggle.setText(registerMode ? "已有账号？登录" : "没有账号？注册");
            showStatus("");
        });
        form.addView(toggle);

        scroll.addView(form);
        setContentView(scroll);
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

    private TextView text(String s, int sp, int color) {
        TextView t = new TextView(this);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER);
        t.setPadding(0, dp(10), 0, dp(10));
        return t;
    }

    private EditText field(String hint, boolean password) {
        EditText e = new EditText(this);
        e.setHint(hint);
        e.setSingleLine(true);
        e.setTextColor(Color.WHITE);
        e.setHintTextColor(Color.GRAY);
        e.setInputType(password ? InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD
                                : InputType.TYPE_CLASS_TEXT);
        return e;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }
}
