CREATE TABLE IF NOT EXISTS conversations (
    thread_id INTEGER PRIMARY KEY,
    other_uid INTEGER NOT NULL,
    last_server_id INTEGER NOT NULL DEFAULT 0,
    last_time TEXT NOT NULL DEFAULT '',
    summary TEXT NOT NULL DEFAULT '',
    hidden_before_id INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS messages (
    local_id INTEGER PRIMARY KEY,
    thread_id INTEGER NOT NULL,
    sender_uid INTEGER NOT NULL,
    server_id INTEGER,
    unique_id TEXT,
    content TEXT NOT NULL,
    message_type INTEGER NOT NULL,
    chat_time TEXT NOT NULL,
    server_status INTEGER NOT NULL DEFAULT 0,
    send_state INTEGER NOT NULL DEFAULT 0,
    local_image BLOB,
    UNIQUE(thread_id, server_id)
);
CREATE UNIQUE INDEX IF NOT EXISTS messages_uuid
    ON messages(thread_id, sender_uid, unique_id) WHERE unique_id IS NOT NULL;
CREATE INDEX IF NOT EXISTS messages_page ON messages(thread_id, server_id DESC);
CREATE INDEX IF NOT EXISTS messages_history_order
    ON messages(thread_id, COALESCE(server_id,2147483647) DESC, local_id DESC);
CREATE INDEX IF NOT EXISTS messages_image_blob ON messages(content)
    WHERE message_type=1 AND server_id IS NOT NULL AND local_image IS NOT NULL;
CREATE TABLE IF NOT EXISTS sync_state (
    thread_id INTEGER PRIMARY KEY,
    confirmed_cursor INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS drafts (
    thread_id INTEGER NOT NULL,
    ordinal INTEGER NOT NULL,
    message_flag TEXT NOT NULL,
    content TEXT NOT NULL,
    image_data BLOB,
    PRIMARY KEY(thread_id, ordinal)
);
CREATE TABLE IF NOT EXISTS attachments (
    file_id TEXT PRIMARY KEY,
    path TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    size INTEGER NOT NULL
);
PRAGMA user_version = 1;
