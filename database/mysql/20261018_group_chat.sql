-- Group metadata; membership remains in group_chat_member. No runtime DDL.
CREATE TABLE IF NOT EXISTS `group_chat` (
    `thread_id` INT UNSIGNED NOT NULL,
    `name` VARCHAR(64) NOT NULL,
    `owner_uid` INT UNSIGNED NOT NULL,
    `creator_uid` INT UNSIGNED NOT NULL,
    `creation_key` VARCHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
    `creation_spec` TEXT NOT NULL,
    `active` BOOLEAN NOT NULL DEFAULT TRUE,
    PRIMARY KEY (`thread_id`),
    UNIQUE KEY `uk_group_creation` (`creator_uid`, `creation_key`),
    CONSTRAINT `fk_group_thread` FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_group_owner` FOREIGN KEY (`owner_uid`) REFERENCES `users` (`id`),
    CONSTRAINT `fk_group_creator` FOREIGN KEY (`creator_uid`) REFERENCES `users` (`id`)
) ENGINE=InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
