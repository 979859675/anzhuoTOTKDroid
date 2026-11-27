<?php
// The app's endpoints: POST api.php?action=register|login|check|logout with a JSON body.
// Every answer is JSON {ok, error?, ...}. Errors use HTTP codes: 400 bad input, 401 wrong login or token,
// 403 disabled or too many devices, 409 name taken, 503 registration closed.
require __DIR__ . '/../src/common.php';

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') json_out(405, ['ok' => false, 'error' => 'method']);

$action = is_string($_GET['action'] ?? null) ? $_GET['action'] : '';
$in = read_json();

switch ($action) {
    case 'register': register($in); break;
    case 'login':    login($in);    break;
    case 'check':    check($in);    break;
    case 'logout':   logout($in);   break;
    default:         json_out(404, ['ok' => false, 'error' => 'unknown action']);
}

function register(array $in): void {
    $passphrase = str_field($in, 'passphrase');   // the registration passphrase set in the web admin
    $user = str_field($in, 'username');
    $pw = str_field($in, 'password');
    if (!valid_username($user)) json_out(400, ['ok' => false, 'error' => 'bad username']);
    if (!valid_password($pw)) json_out(400, ['ok' => false, 'error' => 'bad password']);

    $row = db()->query("SELECT v FROM settings WHERE k = 'reg_passphrase_hash'")->fetch();
    if (!$row) json_out(503, ['ok' => false, 'error' => 'registration closed']);
    if (!password_verify($passphrase, $row['v'])) {
        usleep(800000);  // slows guessing of the passphrase
        json_out(401, ['ok' => false, 'error' => 'wrong passphrase']);
    }

    try {
        db()->prepare('INSERT INTO users (username, pass_hash, disabled, created_at) VALUES (?, ?, 0, ?)')
            ->execute([$user, password_hash($pw, PASSWORD_DEFAULT), now_utc()]);
    } catch (PDOException $e) {
        if ($e->getCode() === '23000') json_out(409, ['ok' => false, 'error' => 'name taken']);
        throw $e;
    }
    json_out(200, ['ok' => true]);
}

function login(array $in): void {
    $user = str_field($in, 'username');
    $pw = str_field($in, 'password');
    $device = str_field($in, 'device_id');
    if (!valid_username($user) || !valid_password($pw) || !valid_device($device)) {
        json_out(400, ['ok' => false, 'error' => 'bad input']);
    }

    $st = db()->prepare('SELECT id, username, pass_hash, disabled FROM users WHERE username = ?');
    $st->execute([$user]);
    $u = $st->fetch();
    // (the same check whether or not the name exists)
    $hash = $u ? $u['pass_hash'] : '$2y$10$abcdefghijklmnopqrstuuO8fYWzwGJfdpvLOYfLSHyYVsU1xaHyK';
    if (!password_verify($pw, $hash) || !$u) {
        usleep(800000);
        json_out(401, ['ok' => false, 'error' => 'wrong login']);
    }
    if ((int)$u['disabled'] === 1) json_out(403, ['ok' => false, 'error' => 'disabled']);

    $max = (int)cfg()['max_devices'];
    $pdo = db();
    $pdo->beginTransaction();
    try {
        $st = $pdo->prepare('SELECT id FROM devices WHERE user_id = ? AND device_id = ? FOR UPDATE');
        $st->execute([$u['id'], $device]);
        $dev = $st->fetch();
        if (!$dev) {
            $cnt = $pdo->prepare('SELECT COUNT(*) AS n FROM devices WHERE user_id = ? FOR UPDATE');
            $cnt->execute([$u['id']]);
            if ((int)$cnt->fetch()['n'] >= $max) {
                $pdo->rollBack();
                json_out(403, ['ok' => false, 'error' => 'device limit']);
            }
            $pdo->prepare('INSERT INTO devices (user_id, device_id, created_at) VALUES (?, ?, ?)')
                ->execute([$u['id'], $device, now_utc()]);
            $dev_id = (int)$pdo->lastInsertId();
        } else {
            $dev_id = (int)$dev['id'];
        }
        // one token per device; a new login on the same device replaces the old token
        $token = bin2hex(random_bytes(32));
        $expires = gmdate('Y-m-d H:i:s', time() + 86400 * (int)cfg()['token_days']);
        $pdo->prepare('UPDATE devices SET token_hash = ?, token_expires = ?, last_seen = ? WHERE id = ?')
            ->execute([sha($token), $expires, now_utc(), $dev_id]);
        $pdo->commit();
    } catch (Throwable $e) {
        if ($pdo->inTransaction()) $pdo->rollBack();
        throw $e;
    }
    json_out(200, ['ok' => true, 'token' => $token, 'username' => $u['username']]);
}

// Called at every launch while online. Failure sends the app back to the login page.
function check(array $in): void {
    $device = str_field($in, 'device_id');
    $token = str_field($in, 'token');
    if (!valid_device($device) || strlen($token) !== 64) json_out(401, ['ok' => false, 'error' => 'no session']);

    $st = db()->prepare(
        'SELECT d.id, u.disabled, d.token_expires FROM devices d JOIN users u ON u.id = d.user_id
         WHERE d.device_id = ? AND d.token_hash = ?'
    );
    $st->execute([$device, sha($token)]);
    $r = $st->fetch();
    if (!$r) json_out(401, ['ok' => false, 'error' => 'no session']);
    if ((int)$r['disabled'] === 1) json_out(403, ['ok' => false, 'error' => 'disabled']);
    if ($r['token_expires'] === null || $r['token_expires'] < now_utc()) json_out(401, ['ok' => false, 'error' => 'expired']);

    db()->prepare('UPDATE devices SET last_seen = ? WHERE id = ?')->execute([now_utc(), $r['id']]);
    json_out(200, ['ok' => true]);
}

function logout(array $in): void {
    $device = str_field($in, 'device_id');
    $token = str_field($in, 'token');
    if (valid_device($device) && strlen($token) === 64) {
        db()->prepare('UPDATE devices SET token_hash = NULL, token_expires = NULL WHERE device_id = ? AND token_hash = ?')
            ->execute([$device, sha($token)]);
    }
    json_out(200, ['ok' => true]);
}
