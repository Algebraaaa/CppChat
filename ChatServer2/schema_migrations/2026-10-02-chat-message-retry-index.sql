-- Optional lookup index for the row-lock based retry implementation.
-- Run on the shared ChatServer MySQL schema after a backup; no rows are changed.
-- The prefix covers all UUIDs accepted by AddChatMsg (at most 64 bytes).
SET @chat_retry_prefix = (
  SELECT LEAST(64, character_maximum_length)
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'chat_message' AND column_name = 'unique_id'
);
SET @chat_retry_ddl = (
  SELECT IF(COUNT(*) > 0, 'SELECT 1',
    CONCAT('CREATE INDEX idx_chat_message_retry ON chat_message (thread_id, sender_id, unique_id(',
           @chat_retry_prefix, '))'))
  FROM information_schema.statistics
  WHERE table_schema = DATABASE() AND table_name = 'chat_message' AND index_name = 'idx_chat_message_retry'
);
PREPARE chat_retry_stmt FROM @chat_retry_ddl;
EXECUTE chat_retry_stmt;
DEALLOCATE PREPARE chat_retry_stmt;
