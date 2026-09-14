-- Phase 8: User interaction tracking for content-based recommendations.
CREATE TABLE user_interactions (
    id INTEGER PRIMARY KEY,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    lot_id INTEGER NOT NULL REFERENCES auctions(id) ON DELETE CASCADE,
    interaction_type TEXT NOT NULL CHECK (interaction_type IN ('view', 'bid')),
    weight REAL NOT NULL CHECK (weight > 0),
    created_at TEXT NOT NULL CHECK (length(created_at) = 20 AND julianday(created_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', created_at, '+0 days') = created_at)
) STRICT;

CREATE INDEX idx_user_interactions_user ON user_interactions(user_id);
CREATE INDEX idx_user_interactions_lot ON user_interactions(lot_id);
