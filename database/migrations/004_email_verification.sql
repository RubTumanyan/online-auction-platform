-- Phase 7 adds email verification. Users created before this migration default
-- to unverified as a conservative choice; the demo accounts are marked verified
-- by AuthService::ensureDemoUsers on startup.
ALTER TABLE users ADD COLUMN email_verified INTEGER NOT NULL DEFAULT 0 CHECK (email_verified IN (0, 1));

-- One active verification code per user. Old codes are deleted when a new code is
-- issued, so the active row is always queryable by user_id (newest first).
CREATE TABLE email_verification_codes (
    id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    code_hash TEXT NOT NULL CHECK (length(code_hash) = 64),
    expires_at TEXT NOT NULL CHECK (length(expires_at) = 20 AND julianday(expires_at) IS NOT NULL),
    failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK (failed_attempts >= 0),
    send_count INTEGER NOT NULL DEFAULT 1 CHECK (send_count >= 1),
    last_sent_at TEXT NOT NULL CHECK (length(last_sent_at) = 20 AND julianday(last_sent_at) IS NOT NULL)
) STRICT;

CREATE INDEX idx_email_verification_codes_user ON email_verification_codes(user_id);