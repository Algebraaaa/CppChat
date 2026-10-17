-- Run through migrate_message_idempotency.py for preflight and resumable steps.
-- Separate INPLACE operations avoid MySQL 8.0 copying the whole table.
ALTER TABLE chat_message
    ADD COLUMN dedup_unique_id VARCHAR(64) GENERATED ALWAYS AS
        (CASE WHEN OCTET_LENGTH(unique_id) = 0 THEN NULL ELSE unique_id END) VIRTUAL,
    ALGORITHM=INPLACE, LOCK=NONE;

ALTER TABLE chat_message
    ADD UNIQUE INDEX uk_chat_message_idempotency (thread_id, sender_id, dedup_unique_id),
    ALGORITHM=INPLACE, LOCK=NONE;
