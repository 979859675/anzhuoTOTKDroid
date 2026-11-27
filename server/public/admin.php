<?php
// Web admin: set the registration passphrase, list accounts, disable / enable, clear the device list.
// Open https://help.xzbbk.cn/admin.php (HTTPS only; see DEPLOY.md). Password: cfg()['admin_hash'].
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
        . 'td,th{border:1px solid #bbb;padding:4px 8px}input{padding:4px}</style></head><body>' . $body . '</body></html>';
    exit;
}

// logout
if (($_GET['logout'] ?? '') === '1') {
    $_SESSION = [];
    session_destroy();
    header('Location: admin.php');
    exit;
}

// not logged in: the login form
if (empty($_SESSION['admin'])) {
    if (($_SERVER['REQUEST_METHOD'] === 'POST') && isset($_POST['password'])) {
        $hash = cfg()['admin_hash'] ?? '';
        if ($hash !== '' && password_verify((string)$_POST['password'], $hash)) {
            session_regenerate_id(true);
            $_SESSION['admin'] = 1;
            header('Location: admin.php');
            exit;
        }
        usleep(1000000);
        page('<p style="color:#b00">密码错误</p><p><a href="admin.php">返回</a></p>');
    }
    page('<h2>账号后台</h2><form method="post"><input type="password" name="password" placeholder="后台密码" required>'
        . ' <button>登录</button></form>');
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

$has_pass = db()->query("SELECT COUNT(*) AS n FROM settings WHERE k = 'reg_passphrase_hash'")->fetch()['n'] > 0;
$rows = db()->query(
    'SELECT u.id, u.username, u.disabled, u.created_at, COUNT(d.id) AS devices
     FROM users u LEFT JOIN devices d ON d.user_id = u.id GROUP BY u.id ORDER BY u.id'
)->fetchAll();

$out = '<h2>账号后台</h2>';
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
