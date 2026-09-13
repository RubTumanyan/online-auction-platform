-- Phase 2 schema v1. Applied with the seed in one transaction by Database.cpp.
-- UTC timestamps use the sortable ISO 8601 form YYYY-MM-DDTHH:MM:SSZ.
CREATE TABLE categories (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE CHECK (length(trim(name)) > 0),
    slug TEXT NOT NULL UNIQUE CHECK (length(trim(slug)) > 0)
) STRICT;

CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL CHECK (length(trim(display_name)) > 0),
    email TEXT NOT NULL COLLATE NOCASE UNIQUE CHECK (length(trim(email)) > 0),
    password_hash TEXT NOT NULL CHECK (length(trim(password_hash)) > 0),
    created_at TEXT NOT NULL CHECK (length(created_at) = 20 AND julianday(created_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', created_at, '+0 days') = created_at),
    updated_at TEXT NOT NULL CHECK (length(updated_at) = 20 AND julianday(updated_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', updated_at, '+0 days') = updated_at),
    CHECK (updated_at >= created_at)
) STRICT;

CREATE TABLE auctions (
    id INTEGER PRIMARY KEY,
    title TEXT NOT NULL CHECK (length(trim(title)) > 0),
    description TEXT NOT NULL CHECK (length(trim(description)) > 0),
    image_url TEXT NOT NULL UNIQUE CHECK (
        (image_url GLOB '/images/products/*.jpg' OR image_url GLOB '/images/products/*.png')
        AND instr(image_url, '..') = 0 AND instr(image_url, '\') = 0
        AND instr(substr(image_url, 18), '/') = 0 AND image_url NOT GLOB '*[?#]*'),
    category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT,
    starting_price_cents INTEGER NOT NULL CHECK (starting_price_cents > 0),
    current_price_cents INTEGER NOT NULL CHECK (current_price_cents > 0
        AND current_price_cents >= starting_price_cents),
    current_winner_id INTEGER REFERENCES users(id) ON DELETE RESTRICT,
    starts_at TEXT NOT NULL CHECK (length(starts_at) = 20 AND julianday(starts_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', starts_at, '+0 days') = starts_at),
    ends_at TEXT NOT NULL CHECK (length(ends_at) = 20 AND julianday(ends_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', ends_at, '+0 days') = ends_at),
    status TEXT NOT NULL CHECK (status IN ('active', 'ended')),
    created_at TEXT NOT NULL CHECK (length(created_at) = 20 AND julianday(created_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', created_at, '+0 days') = created_at),
    updated_at TEXT NOT NULL CHECK (length(updated_at) = 20 AND julianday(updated_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', updated_at, '+0 days') = updated_at),
    CHECK (ends_at > starts_at),
    CHECK (updated_at >= created_at)
) STRICT;

CREATE TABLE bids (
    id INTEGER PRIMARY KEY,
    auction_id INTEGER NOT NULL REFERENCES auctions(id) ON DELETE RESTRICT,
    user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
    created_at TEXT NOT NULL CHECK (length(created_at) = 20 AND julianday(created_at) IS NOT NULL
        AND strftime('%Y-%m-%dT%H:%M:%SZ', created_at, '+0 days') = created_at)
) STRICT;

CREATE INDEX idx_auctions_active_category ON auctions(category_id, id) WHERE status = 'active';
CREATE INDEX idx_auctions_current_price ON auctions(current_price_cents, id);
CREATE INDEX idx_auctions_ends_at ON auctions(ends_at, id);
CREATE INDEX idx_bids_auction_created ON bids(auction_id, created_at, id);
CREATE INDEX idx_bids_user ON bids(user_id);
