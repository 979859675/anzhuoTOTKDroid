-- Account database (MySQL 8.0). Import it into an empty database made in the BT panel (utf8mb4).
-- Change nothing here by hand: the registration passphrase and accounts are managed in admin.php.

CREATE TABLE IF NOT EXISTS users (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(20) NOT NULL,
  pass_hash VARCHAR(255) NOT NULL,
  disabled TINYINT(1) NOT NULL DEFAULT 0,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- one row per (account, device); token_hash is the SHA-256 of the device's current token (never the token itself)
CREATE TABLE IF NOT EXISTS devices (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  user_id INT UNSIGNED NOT NULL,
  device_id VARCHAR(64) NOT NULL,
  token_hash CHAR(64) NULL,
  token_expires DATETIME NULL,
  last_seen DATETIME NULL,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_user_device (user_id, device_id),
  KEY ix_token (token_hash),
  CONSTRAINT fk_devices_user FOREIGN KEY (user_id) REFERENCES users (id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- k / v: 'reg_passphrase_hash' (absent = registration closed)
CREATE TABLE IF NOT EXISTS settings (
  k VARCHAR(32) NOT NULL PRIMARY KEY,
  v VARCHAR(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
