-- Administrator migration; independent of second_hand_platform. Never drop existing data.
CREATE DATABASE IF NOT EXISTS asynlog_storage CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;
USE asynlog_storage;
CREATE TABLE IF NOT EXISTS users (
    id CHAR(34) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    email VARCHAR(254) NOT NULL DEFAULT '',
    password_hash VARCHAR(255) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS sessions (
    token_hash CHAR(64) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    user_id CHAR(34) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    created_at DATETIME NOT NULL,
    expires_at DATETIME NOT NULL,
    CONSTRAINT fk_sessions_user FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE,
    INDEX sessions_expiry(expires_at)
) ENGINE=InnoDB;
-- Provision storage_app@127.0.0.1 separately using a locally generated password.
-- GRANT SELECT ON asynlog_storage.users TO 'storage_app'@'127.0.0.1';
-- GRANT SELECT, INSERT, DELETE ON asynlog_storage.sessions TO 'storage_app'@'127.0.0.1';
-- User provisioning/migrations/tests use a separate administrator or test account.
