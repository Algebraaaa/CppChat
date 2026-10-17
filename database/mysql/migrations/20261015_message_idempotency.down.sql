-- Removing this constraint does not remove or rewrite message rows.
ALTER TABLE chat_message
    DROP INDEX uk_chat_message_idempotency,
    ALGORITHM=INPLACE, LOCK=NONE;

ALTER TABLE chat_message
    DROP COLUMN dedup_unique_id,
    ALGORITHM=INPLACE, LOCK=NONE;
