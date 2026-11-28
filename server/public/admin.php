<?php
// Web admin: set the registration passphrase, list accounts, disable / enable, clear the device list.
// Open https://ly.xzcqgzs.cn/admin.php (HTTPS only; see DEPLOY.md).
// The admin is set up on the first visit: the first account name and password entered here become the admin account.
require __DIR__ . '/../src/common.php';

ini_set('session.cookie_httponly', '1');
ini_set('session.cookie_secure', '1');
ini_set('session.cookie_samesite', 'Strict');
session_start();
header('X-Frame-Options: DENY');
header('Content-Security-Policy: default-src \'self\' \'unsafe-inline\'; frame-ancestors \'none\'');
header('Content-Type: text/html; charset=utf-8');
header('Referrer-Policy: same-origin');

function h(string $s): string { return htmlspecialchars($s, ENT_QUOTES, 'UTF-8'); }

function csrf(): string {
    if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(16));
    return $_SESSION['csrf'];
}

function flash(string $text, bool $err = false): string {
    return '<div class="msg' . ($err ? ' err' : '') . '">' . h($text) . '</div>';
}

// The page: one stylesheet for phones and computers (adapts to the screen width; follows the system's dark mode).
function page(string $body, bool $login = false): void {
    $css = <<<'CSS'
:root{--bg:#f3f4f6;--card:#fff;--ink:#1d2129;--muted:#6b7280;--line:#e5e7eb;--accent:#b8862b;--accent-ink:#fff;--danger:#c0392b;--ok:#1e8e3e}
@media (prefers-color-scheme:dark){:root{--bg:#0f1115;--card:#171a21;--ink:#e8eaee;--muted:#9aa3ae;--line:#2a2f38;--accent:#d9b26a;--accent-ink:#101418}}
*{box-sizing:border-box}
body{margin:0;font:15px/1.5 -apple-system,BlinkMacSystemFont,"PingFang SC","Microsoft YaHei","Noto Sans CJK SC",sans-serif;background:var(--bg);color:var(--ink);-webkit-text-size-adjust:100%}
.wrap{max-width:980px;margin:0 auto;padding:14px}
.top{display:flex;flex-wrap:wrap;align-items:center;justify-content:space-between;gap:8px;margin-bottom:14px}
.top h1{font-size:20px;margin:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px;margin-bottom:14px}
.card h2{font-size:16px;margin:0 0 10px}
.muted{color:var(--muted);font-size:13px}
input[type=text],input[type=password]{width:100%;padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:transparent;color:var(--ink);font:inherit}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.row input{flex:1 1 200px;min-width:0}
.stack{display:flex;flex-direction:column;gap:10px}
button,.btn{padding:10px 16px;border:0;border-radius:8px;background:var(--accent);color:var(--accent-ink);font:inherit;font-size:14px;cursor:pointer;text-decoration:none;display:inline-block}
button.ghost,.btn.ghost{background:transparent;color:var(--ink);border:1px solid var(--line)}
button.warn{background:var(--danger);color:#fff}
button:disabled{opacity:.5;cursor:default}
.msg{padding:10px 12px;border-radius:8px;margin-bottom:12px;background:rgba(30,142,62,.12);color:var(--ok)}
.msg.err{background:rgba(192,57,43,.12);color:var(--danger)}
.badge{display:inline-block;padding:2px 9px;border-radius:999px;font-size:12px}
.badge.on{background:rgba(30,142,62,.15);color:var(--ok)}
.badge.off{background:rgba(192,57,43,.15);color:var(--danger)}
.tbl{width:100%;border-collapse:collapse}
.tbl th,.tbl td{padding:10px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle;font-size:14px}
.tbl th{color:var(--muted);font-weight:600}
.acts{display:flex;flex-wrap:wrap;gap:6px}
.acts form{margin:0}
.login{max-width:380px;margin:10vh auto 0;padding:0 14px}
.login h1{font-size:22px;margin:0 0 4px}
@media (max-width:640px){
 .tbl thead{display:none}
 .tbl,.tbl tbody,.tbl tr,.tbl td{display:block;width:100%}
 .tbl tr{border:1px solid var(--line);border-radius:10px;padding:8px 12px;margin-bottom:10px;background:var(--card)}
 .tbl td{border:0;padding:5px 0;display:flex;justify-content:space-between;align-items:center;gap:12px}
 .tbl td::before{content:attr(data-label);color:var(--muted);flex:0 0 76px}
 .tbl td.acts-cell{display:block}
 .tbl td.acts-cell::before{content:none}
 .acts button{flex:1 1 auto}
}
CSS;
    $cls = $login ? 'login' : 'wrap';
    echo '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">'
        . '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        . '<meta name="robots" content="noindex,nofollow"><title>账号后台</title><style>' . $css . '</style></head>'
        . '<body><div class="' . $cls . '">' . $body . '</div></body></html>';
    exit;
}

// log out
if (($_GET['logout'] ?? '') === '1') {
    $_SESSION = [];
    session_destroy();
    header('Location: admin.php');
    exit;
}

$has_admin = (int)db()->query('SELECT COUNT(*) AS n FROM admins')->fetch()['n'] > 0;

// not logged in: the setup form (first visit) or the login form
if (empty($_SESSION['admin'])) {
    if ($_SERVER['REQUEST_METHOD'] === 'POST') {
        $u = (string)($_POST['username'] ?? '');
        $p = (string)($_POST['password'] ?? '');

        if (!$has_admin) {
            // first visit: this account name and password become the admin account (only if none exists yet)
            if (!valid_username($u) || strlen($p) < 8 || strlen($p) > 72) {
                page('<div class="card">' . flash('账户需 3-20 位（字母、数字、下划线），密码需 8-72 位', true)
                    . '<a class="btn ghost" href="admin.php">返回</a></div>', true);
            }
            $st = db()->prepare(
                'INSERT INTO admins (username, pass_hash, created_at)
                 SELECT ?, ?, ? FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM admins)'
            );
            $st->execute([$u, password_hash($p, PASSWORD_DEFAULT), now_utc()]);
            if ($st->rowCount() !== 1) {
                page('<div class="card">' . flash('管理员已设置，请用管理员账户登录。')
                    . '<a class="btn" href="admin.php">去登录</a></div>', true);
            }
            session_regenerate_id(true);
            $_SESSION['admin'] = $u;
            header('Location: admin.php');
            exit;
        }

        $st = db()->prepare('SELECT username, pass_hash FROM admins WHERE username = ?');
        $st->execute([$u]);
        $a = $st->fetch();
        if ($a && $p !== '' && password_verify($p, $a['pass_hash'])) {
            session_regenerate_id(true);
            $_SESSION['admin'] = $a['username'];
            header('Location: admin.php');
            exit;
        }
        usleep(1000000);
        page('<div class="card">' . flash('账户或密码错误', true)
            . '<a class="btn ghost" href="admin.php">返回</a></div>', true);
    }

    if (!$has_admin) {
        page('<div class="card"><h1>账号后台</h1><p class="muted">首次设置：输入的账户和密码将成为管理员账户，请尽快完成设置。</p>'
            . '<form method="post" class="stack"><input type="text" name="username" placeholder="管理员账户（3-20 位）" required autocomplete="username">'
            . '<input type="password" name="password" placeholder="管理员密码（至少 8 位）" required autocomplete="new-password">'
            . '<button>设置管理员</button></form></div>', true);
    }
    page('<div class="card"><h1>账号后台</h1><p class="muted">请输入管理员账户和密码</p>'
        . '<form method="post" class="stack"><input type="text" name="username" placeholder="管理员账户" required autocomplete="username">'
        . '<input type="password" name="password" placeholder="密码" required autocomplete="current-password">'
        . '<button>登录</button></form></div>', true);
}

// logged in: check the CSRF token on every change
$msg = '';
$msg_err = false;
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    if (!hash_equals(csrf(), (string)($_POST['csrf'] ?? ''))) page('<div class="card">' . flash('表单已过期，请刷新。', true) . '</div>');
    $act = (string)($_POST['act'] ?? '');
    $db = db();
    if ($act === 'set_pass') {
        $p = (string)($_POST['passphrase'] ?? '');
        if (strlen($p) < 8) {
            $msg = '注册口令至少 8 位';
            $msg_err = true;
        } else {
            $db->prepare("REPLACE INTO settings (k, v) VALUES ('reg_passphrase_hash', ?)")
                ->execute([password_hash($p, PASSWORD_DEFAULT)]);
            $msg = '注册口令已更新';
        }
    } elseif ($act === 'del_pass') {
        $db->prepare("DELETE FROM settings WHERE k = 'reg_passphrase_hash'")->execute();
        $msg = '注册口令已删除，注册已关闭';
    } elseif ($act === 'toggle' || $act === 'reset_devices') {
        $id = (int)($_POST['id'] ?? 0);
        if ($id > 0 && $act === 'toggle') {
            $db->prepare('UPDATE users SET disabled = 1 - disabled WHERE id = ?')->execute([$id]);
            $msg = '已切换启用状态';
        } elseif ($id > 0) {
            $db->prepare('DELETE FROM devices WHERE user_id = ?')->execute([$id]);
            $msg = '已清空设备绑定';
        }
    }
}

$has_pass = (int)db()->query("SELECT COUNT(*) AS n FROM settings WHERE k = 'reg_passphrase_hash'")->fetch()['n'] > 0;
$rows = db()->query(
    'SELECT u.id, u.username, u.disabled, u.created_at, COUNT(d.id) AS devices
     FROM users u LEFT JOIN devices d ON d.user_id = u.id GROUP BY u.id ORDER BY u.id'
)->fetchAll();
$csrf = h(csrf());

$out = '<div class="top"><div><h1>账号后台</h1><div class="muted">管理员：' . h((string)$_SESSION['admin']) . '</div></div>'
    . '<a class="btn ghost" href="admin.php?logout=1">退出</a></div>';
if ($msg !== '') $out .= flash($msg, $msg_err);

$out .= '<div class="card"><h2>注册口令</h2>'
    . '<p class="muted">状态：' . ($has_pass
        ? '<span class="badge on">已设置</span>'
        : '<span class="badge off">未设置，注册已关闭</span>') . '</p>'
    . '<form method="post" class="row"><input type="hidden" name="csrf" value="' . $csrf . '">'
    . '<input type="hidden" name="act" value="set_pass">'
    . '<input type="text" name="passphrase" minlength="8" placeholder="' . ($has_pass ? '新的注册口令（至少 8 位）' : '注册口令（至少 8 位）') . '" required autocomplete="off">'
    . '<button>' . ($has_pass ? '修改口令' : '设置口令') . '</button></form>'
    . ($has_pass
        ? '<form method="post" class="row" style="margin-top:10px" onsubmit="return confirm(\'确定删除注册口令？删除后用户无法注册。\')">'
          . '<input type="hidden" name="csrf" value="' . $csrf . '"><input type="hidden" name="act" value="del_pass">'
          . '<button class="warn">删除口令</button></form>'
        : '')
    . '</div>';

$out .= '<div class="card"><h2>账号（' . count($rows) . '）</h2>';
if (!$rows) {
    $out .= '<p class="muted">还没有账号。把注册口令告诉用户，让他们在 App 里注册即可。</p>';
} else {
    $out .= '<table class="tbl"><thead><tr><th>ID</th><th>用户名</th><th>状态</th><th>设备数</th><th>创建时间（UTC）</th><th>操作</th></tr></thead><tbody>';
    foreach ($rows as $r) {
        $off = (int)$r['disabled'] === 1;
        $out .= '<tr><td data-label="ID">' . (int)$r['id'] . '</td>'
            . '<td data-label="用户名">' . h($r['username']) . '</td>'
            . '<td data-label="状态"><span class="badge ' . ($off ? 'off">已禁用' : 'on">正常') . '</span></td>'
            . '<td data-label="设备数">' . (int)$r['devices'] . ' / 2</td>'
            . '<td data-label="创建时间">' . h((string)$r['created_at']) . '</td>'
            . '<td class="acts-cell"><div class="acts">'
            . '<form method="post"><input type="hidden" name="csrf" value="' . $csrf . '">'
            . '<input type="hidden" name="id" value="' . (int)$r['id'] . '">'
            . '<button class="ghost" name="act" value="toggle">' . ($off ? '启用' : '禁用') . '</button></form>'
            . '<form method="post" onsubmit="return confirm(\'确定清空该账号的设备绑定？\')">'
            . '<input type="hidden" name="csrf" value="' . $csrf . '">'
            . '<input type="hidden" name="id" value="' . (int)$r['id'] . '">'
            . '<button class="ghost" name="act" value="reset_devices">清空设备</button></form>'
            . '</div></td></tr>';
    }
    $out .= '</tbody></table>';
}
$out .= '</div>';
page($out);
