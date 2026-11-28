<?php
// Web admin: the registration passphrases (several at once, shown here, with how many registered under each),
// the accounts (with the passphrase each one used), disable / enable, clear the device list.
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
.tbl td.acts-cell .acts{justify-content:flex-start}
 .tbl td.acts-cell::before{content:none}
 .acts button{flex:1 1 auto}
}
.pp-input{width:150px;flex:0 1 150px}
code{font:13px ui-monospace,Menlo,Consolas,monospace;background:var(--line);padding:2px 6px;border-radius:6px;word-break:break-all}
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
    $id = (int)($_POST['id'] ?? 0);
    $db = db();
    if ($act === 'add_pass' || $act === 'edit_pass') {
        $phrase = trim((string)($_POST['phrase'] ?? ''));
        if (!valid_passphrase($phrase)) {
            $msg = '口令需 8-64 位';
            $msg_err = true;
        } else {
            try {
                if ($act === 'add_pass') {
                    $db->prepare('INSERT INTO passphrases (phrase, enabled, created_at) VALUES (?, 1, ?)')
                        ->execute([$phrase, now_utc()]);
                    $msg = '口令已添加';
                } elseif ($id > 0) {
                    $db->prepare('UPDATE passphrases SET phrase = ? WHERE id = ?')->execute([$phrase, $id]);
                    $msg = '口令已修改';
                }
            } catch (PDOException $e) {
                if ($e->getCode() !== '23000') throw $e;
                $msg = '这个口令已经存在';
                $msg_err = true;
            }
        }
    } elseif ($act === 'toggle_pass' && $id > 0) {
        $db->prepare('UPDATE passphrases SET enabled = 1 - enabled WHERE id = ?')->execute([$id]);
        $msg = '已切换口令的启用状态';
    } elseif ($act === 'del_pass' && $id > 0) {
        $db->prepare('DELETE FROM passphrases WHERE id = ?')->execute([$id]);
        $msg = '口令已删除（已注册的账号不受影响）';
    } elseif ($act === 'toggle' && $id > 0) {
        $db->prepare('UPDATE users SET disabled = 1 - disabled WHERE id = ?')->execute([$id]);
        $msg = '已切换账号的启用状态';
    } elseif ($act === 'reset_devices' && $id > 0) {
        $db->prepare('DELETE FROM devices WHERE user_id = ?')->execute([$id]);
        $msg = '已清空设备绑定';
    }
}

$csrf = h(csrf());
$passes = db()->query(
    'SELECT p.id, p.phrase, p.enabled, p.created_at, COUNT(u.id) AS n
     FROM passphrases p LEFT JOIN users u ON u.passphrase_id = p.id
     GROUP BY p.id, p.phrase, p.enabled, p.created_at ORDER BY p.id'
)->fetchAll();
$rows = db()->query(
    'SELECT u.id, u.username, u.disabled, u.created_at, p.phrase, COUNT(d.id) AS devices
     FROM users u
     LEFT JOIN passphrases p ON p.id = u.passphrase_id
     LEFT JOIN devices d ON d.user_id = u.id
     GROUP BY u.id, u.username, u.disabled, u.created_at, p.phrase ORDER BY u.id'
)->fetchAll();

$out = '<div class="top"><div><h1>账号后台</h1><div class="muted">管理员：' . h((string)$_SESSION['admin']) . '</div></div>'
    . '<a class="btn ghost" href="admin.php?logout=1">退出</a></div>';
if ($msg !== '') $out .= flash($msg, $msg_err);

// the registration passphrases
$open = 0;
foreach ($passes as $p) if ((int)$p['enabled'] === 1) $open++;
$out .= '<div class="card"><h2>注册口令（' . count($passes) . '）</h2>'
    . '<p class="muted">' . ($open > 0
        ? '当前启用 ' . $open . ' 个。用户注册时输入其中任意一个启用的口令即可。'
        : '<span class="badge off">没有启用的口令，注册已关闭</span>') . '</p>'
    . '<form method="post" class="row"><input type="hidden" name="csrf" value="' . $csrf . '">'
    . '<input type="hidden" name="act" value="add_pass">'
    . '<input type="text" name="phrase" minlength="8" maxlength="64" placeholder="新口令（8-64 位）" required autocomplete="off">'
    . '<button>添加口令</button></form>';
if (!$passes) {
    $out .= '<p class="muted" style="margin-top:12px">还没有口令。</p>';
} else {
    $out .= '<table class="tbl" style="margin-top:12px"><thead><tr><th>ID</th><th>口令</th><th>状态</th><th>注册人数</th><th>创建时间（UTC）</th><th>操作</th></tr></thead><tbody>';
    foreach ($passes as $p) {
        $on = (int)$p['enabled'] === 1;
        $out .= '<tr><td data-label="ID">' . (int)$p['id'] . '</td>'
            . '<td data-label="口令"><code>' . h($p['phrase']) . '</code></td>'
            . '<td data-label="状态"><span class="badge ' . ($on ? 'on">启用' : 'off">停用') . '</span></td>'
            . '<td data-label="注册人数">' . (int)$p['n'] . ' 人</td>'
            . '<td data-label="创建时间">' . h((string)$p['created_at']) . '</td>'
            . '<td class="acts-cell"><div class="acts">'
            . '<form method="post" class="row"><input type="hidden" name="csrf" value="' . $csrf . '">'
            . '<input type="hidden" name="act" value="edit_pass"><input type="hidden" name="id" value="' . (int)$p['id'] . '">'
            . '<input type="text" class="pp-input" name="phrase" minlength="8" maxlength="64" value="' . h($p['phrase']) . '" required autocomplete="off">'
            . '<button class="ghost">修改</button></form>'
            . '<form method="post"><input type="hidden" name="csrf" value="' . $csrf . '">'
            . '<input type="hidden" name="id" value="' . (int)$p['id'] . '">'
            . '<button class="ghost" name="act" value="toggle_pass">' . ($on ? '停用' : '启用') . '</button></form>'
            . '<form method="post" onsubmit="return confirm(\'确定删除这个口令？已注册的账号不受影响，但之后不能再用这个口令注册。\')">'
            . '<input type="hidden" name="csrf" value="' . $csrf . '"><input type="hidden" name="id" value="' . (int)$p['id'] . '">'
            . '<button class="warn" name="act" value="del_pass">删除</button></form>'
            . '</div></td></tr>';
    }
    $out .= '</tbody></table>';
}
$out .= '</div>';

// the accounts
$out .= '<div class="card"><h2>账号（' . count($rows) . '）</h2>';
if (!$rows) {
    $out .= '<p class="muted">还没有账号。把注册口令告诉用户，让他们在 App 里注册即可。</p>';
} else {
    $out .= '<table class="tbl"><thead><tr><th>ID</th><th>用户名</th><th>注册口令</th><th>状态</th><th>设备数</th><th>创建时间（UTC）</th><th>操作</th></tr></thead><tbody>';
    foreach ($rows as $r) {
        $off = (int)$r['disabled'] === 1;
        $out .= '<tr><td data-label="ID">' . (int)$r['id'] . '</td>'
            . '<td data-label="用户名">' . h($r['username']) . '</td>'
            . '<td data-label="注册口令">' . ($r['phrase'] !== null ? '<code>' . h($r['phrase']) . '</code>' : '<span class="muted">已删除的口令</span>') . '</td>'
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
