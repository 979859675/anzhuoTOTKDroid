-- Upgrade for a database that was made before the multiple passphrases. Run ONCE in phpMyAdmin
-- (database totk_accounts). Running it again fails with "Duplicate column" - that is fine, nothing breaks.
--
-- The old single passphrase was kept only as a hash, so it cannot be shown or moved: after this, add the
-- passphrases again in the admin page (注册口令).

CREATE TABLE IF NOT EXISTS passphrases (
  id INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  phrase VARCHAR(64) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,
  enabled TINYINT(1) NOT NULL DEFAULT 1,
  created_at DATETIME NOT NULL,
  UNIQUE KEY uq_phrase (phrase)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

ALTER TABLE users ADD COLUMN passphrase_id INT UNSIGNED NULL AFTER disabled;
ALTER TABLE users ADD CONSTRAINT fk_users_pass FOREIGN KEY (passphrase_id) REFERENCES passphrases (id) ON DELETE SET NULL;
