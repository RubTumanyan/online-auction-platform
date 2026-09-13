ALTER TABLE users ADD COLUMN username TEXT COLLATE NOCASE;

CREATE UNIQUE INDEX idx_users_username ON users(username);
CREATE TRIGGER users_username_required_insert
BEFORE INSERT ON users
WHEN NEW.username IS NULL OR length(NEW.username) < 3 OR length(NEW.username) > 30
BEGIN
    SELECT RAISE(ABORT, 'username is required');
END;
CREATE TRIGGER users_username_required_update
BEFORE UPDATE OF username ON users
WHEN NEW.username IS NULL OR length(NEW.username) < 3 OR length(NEW.username) > 30
BEGIN
    SELECT RAISE(ABORT, 'username is required');
END;

CREATE TABLE auth_tokens (
    token_hash TEXT PRIMARY KEY CHECK (length(token_hash) = 64),
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at TEXT NOT NULL CHECK (length(expires_at) = 20 AND julianday(expires_at) IS NOT NULL),
    created_at TEXT NOT NULL CHECK (length(created_at) = 20 AND julianday(created_at) IS NOT NULL)
) STRICT;

CREATE INDEX idx_auth_tokens_user ON auth_tokens(user_id);
CREATE INDEX idx_auth_tokens_expires ON auth_tokens(expires_at);
