-- Additive migration for the existing cppchat.chat_message table.
-- Run once against the schema configured by ChatServer's [Mysql] section.
-- Existing rows remain text messages with media_size = 0.
ALTER TABLE chat_message
  ADD COLUMN msg_type TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER status,
  ADD COLUMN media_size BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER msg_type;
