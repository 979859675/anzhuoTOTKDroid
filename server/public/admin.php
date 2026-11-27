<?php
// Web admin: set the registration passphrase, list accounts, disable / enable, clear the device list.
// Open https://help.xzbbk.cn/admin.php (HTTPS only; see DEPLOY.md).
// The admin is set up on the first visit: the first account name and password entered here become the admin account.
require __DIR__ . '/../src/common.php';

ini_set('session.cookie_httponly', '1');
ini_set('session.cookie_secure', '1');
ini_set('session.cookie_samesite', 'Strict');
session_start();
header('X-Frame-Options: DENY');
header('Content-Security-Policy: default-src \'self\' \'unsafe-inline\'; frame-ancestors \'none\'');
header('Content-Type: text/html; charset=utf-8');

function h(string $s): string { return htmlspecialchars($s, ENT_QUOTES, 'UTF-8'); }

function csrf(): string {
    if (empty($_SESSION['csrf'])) $_SESSION['csrf'] = bin2hex(random_bytes(16));
    return $_SESSION['csrf'];
}

function page(string $body): void {
    echo '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
        . '<title>账号后台</title><style>body{font-family:sans-serif;margin:20px}table{border-collapse:collapse}'
        . 'td,th{border:1px solid #bbb;padding:4px 8px}input{padding:4px;margin:2px}</style></head><body>' . $body . '</body></html>';
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
                page('<p style="color:#b00">账户需 3-20 位（字母、数字、下划线），密码需 8-72 位</p><p><a href="admin.php">返回</a></p>');
            }
            $st = db()->prepare(
                'INSERT INTO admins (username, pass_hash, created_at)
                 SELECT ?, ?, ? FROM DUAL WHERE NOT EXISTS (SELECT 1 FROM admins)'
            );
            $st->execute([$u, password_hash($p, PASSWORD_DEFAULT), now_utc()]);
            if ($st->rowCount() !== 1) page('<p>管理员已设置，请用管理员账户登录。</p><p><a href="admin.php">返回</a></p>');
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
        page('<p style="color:#b00">账户或密码错误</p><p><a href="admin.php">返回</a></p>');
    }

    if (!$has_admin) {
        page('<h2>账号后台：首次设置</h2>'
            . '<p>输入的账户和密码将成为管理员账户，请尽快完成设置。</p>'
            . '<form method="post"><input type="text" name="username" placeholder="管理员账户（3-20 位）" required><br>'
            . '<input type="password" name="password" placeholder="管理员密码（至少 8 位）" required><br>'
            . '<button>设置管理员</button></form>');
    }
    page('<h2>账号后台</h2><form method="post"><input type="text" name="username" placeholder="管理员账户" required><br>'
        . '<input type="password" name="password" placeholder="密码" required><br><button>登录</button></form>');
}

// logged in: check the CSRF token on every change
$msg = '';
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    if (!hash_equals(csrf(), (string)($_POST['csrf'] ?? ''))) page('<p>表单已过期，请刷新。</p>');
    $act = (string)($_POST['act'] ?? '');
    $db = db();
    if ($act === 'set_pass') {
        $p = (string)($_POST['passphrase'] ?? '');
        if (strlen($p) < 8) {
            $msg = '注册口令至少 8 位';
        } else {
            $db->prepare("REPLACE INTO settings (k, v) VALUES ('reg_passphrase_hash', ?)")
                ->execute([password_hash($p, PASSWORD_DEFAULT)]);
            $msg = '注册口令已更新';
        }
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

$out = '<h2>账号后台</h2><p>管理员：' . h((string)$_SESSION['admin']) . '</p>';
if ($msg !== '') $out .= '<p><b>' . h($msg) . '</b></p>';
$out .= '<p>注册口令：' . ($has_pass ? '已设置' : '<b>未设置（注册已关闭）</b>') . '</p>';
$out .= '<form method="post"><input type="hidden" name="csrf" value="' . h(csrf()) . '">'
    . '<input type="hidden" name="act" value="set_pass"><input type="text" name="passphrase" minlength="8" placeholder="新的注册口令（至少 8 位）" required>'
    . ' <button>保存</button></form>';
$out .= '<h3>账号（' . count($rows) . '）</h3><table><tr><th>ID</th><th>用户名</th><th>状态</th><th>设备数</th><th>创建时间(UTC)</th><th>操作</th></tr>';
foreach ($rows as $r) {
    $out .= '<tr><td>' . (int)$r['id'] . '</td><td>' . h($r['username']) . '</td><td>'
        . ((int)$r['disabled'] === 1 ? '<b>已禁用</b>' : '正常') . '</td><td>' . (int)$r['devices'] . '</td><td>' . h((string)$r['created_at']) . '</td><td>'
        . '<form method="post" style="display:inline"><input type="hidden" name="csrf" value="' . h(csrf()) . '">'
        . '<input type="hidden" name="id" value="' . (int)$r['id'] . '"><button name="act" value="toggle">'
        . ((int)$r['disabled'] === 1 ? '启用' : '禁用') . '</button> <button name="act" value="reset_devices">清空设备</button></form>'
        . '</td></tr>';
}
$out .= '</table><p><a href="admin.php?logout=1">退出后台</a></p>';
page($out);
