-- Account database (MySQL 8.0). Import it into an empty database made in the BT panel (utf8mb4).
-- A database made before passphrases existed: run server/migrate-passphrases.sql instead (once).

CREATE TABLE IF NOT EXISTS admins (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(20) NOT NULL,
  pass_hash VARCHAR(255) NOT NULL,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_admin (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- registration passphrases: several can be enabled at once; the phrase is kept as typed (the admin page shows it)
CREATE TABLE IF NOT EXISTS passphrases (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  phrase VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  enabled TINYINT(1) NOT NULL DEFAULT 1,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_phrase (phrase)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS users (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username VARCHAR(20) NOT NULL,
  pass_hash VARCHAR(255) NOT NULL,
  disabled TINYINT(1) NOT NULL DEFAULT 0,
  passphrase_id INT UNSIGNED NULL,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_username (username),
  CONSTRAINT fk_users_pass FOREIGN KEY (passphrase_id) REFERENCES passphrases (id) ON DELETE SET NULL
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

CREATE TABLE IF NOT EXISTS settings (
  k VARCHAR(32) NOT NULL PRIMARY KEY,
  v VARCHAR(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
