-- Run once on an existing CppChat database before starting the updated ChatServers.
-- New installations already have these columns in init_cppchat.sql.
ALTER TABLE `chat_message`
    ADD COLUMN `msg_type` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `content`,
    ADD COLUMN `media_size` BIGINT UNSIGNED NOT NULL DEFAULT 0 AFTER `msg_type`;
