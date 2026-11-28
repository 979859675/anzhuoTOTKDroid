<?php
// Shared helpers for the account server (PHP 8.0+, MySQL 8.0). Not web-visible: the web root is server/public/.
declare(strict_types=1);
date_default_timezone_set('UTC');

function cfg(): array {
    static $c = null;
    if ($c === null) {
        $f = __DIR__ . '/../config.php';  // the real config, next to server/ and never in Git (see config.example.php)
        if (!is_file($f)) {
            http_response_code(500);
            header('Content-Type: text/plain; charset=utf-8');
            exit('server not configured');
        }
        $c = require $f;
        // (a config.php without 'return [ ... ];' makes require() give 1: say what to fix, not a TypeError)
        if (!is_array($c) || !isset($c['db'])) {
            http_response_code(500);
            header('Content-Type: text/plain; charset=utf-8');
            exit("server config error: config.php must start with <?php and end with: return [ ... ]; (see DEPLOY.md section 4)");
        }
    }
    return $c;
}

function db(): PDO {
    static $pdo = null;
    if ($pdo === null) {
        $d = cfg()['db'];
        $pdo = new PDO(
            "mysql:host={$d['host']};dbname={$d['name']};charset=utf8mb4",
            $d['user'],
            $d['pass'],
            [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_EMULATE_PREPARES => false]
        );
    }
    return $pdo;
}

function json_out(int $code, array $body): void {
    http_response_code($code);
    header('Content-Type: application/json; charset=utf-8');
    header('Cache-Control: no-store');
    echo json_encode($body, JSON_UNESCAPED_UNICODE);
    exit;
}

// The request body: a JSON object of at most 4 KB (anything bigger is not a login).
function read_json(): array {
    $raw = file_get_contents('php://input', false, null, 0, 4097);
    if ($raw === false || strlen($raw) > 4096) return [];
    $d = json_decode($raw, true);
    return is_array($d) ? $d : [];
}

function str_field(array $in, string $key): string {
    return isset($in[$key]) && is_string($in[$key]) ? $in[$key] : '';
}

function now_utc(): string { return gmdate('Y-m-d H:i:s'); }

function sha(string $s): string { return hash('sha256', $s); }

// Usernames: 3-20 letters, digits or underscore.
function valid_username(string $u): bool { return preg_match('/^[A-Za-z0-9_]{3,20}$/', $u) === 1; }

// Device ids come from the app (Android ID + random UUID): 16-64 safe characters.
function valid_device(string $d): bool { return preg_match('/^[A-Za-z0-9_-]{16,64}$/', $d) === 1; }

// A password: 6-72 bytes (bcrypt reads at most 72).
function valid_password(string $p): bool { return strlen($p) >= 6 && strlen($p) <= 72; }

// A registration passphrase: 8-64 characters (Chinese characters count as one).
function valid_passphrase(string $p): bool {
    $n = function_exists('mb_strlen') ? mb_strlen($p, 'UTF-8') : strlen($p);
    return $n >= 8 && $n <= 64;
}
