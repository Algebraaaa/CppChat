-- CppChat 服务端完整表结构，适用于 MySQL 8.4 的新数据库。
-- 已存在的表不会重复创建，也不会自动升级其结构。
CREATE DATABASE IF NOT EXISTS `cppchat`
    CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `cppchat`;

-- 用户账号与资料；密码使用 PBKDF2 哈希、随机盐和迭代次数。
CREATE TABLE IF NOT EXISTS `users` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `username` VARCHAR(64) NOT NULL,
    `email` VARCHAR(255) NOT NULL,
    `nick` VARCHAR(64) NOT NULL DEFAULT '',
    `profile_description` VARCHAR(255) NOT NULL DEFAULT '',
    `sex` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `icon` VARCHAR(512) NOT NULL DEFAULT '',
    `password_hash` CHAR(64) NOT NULL,
    `password_salt` CHAR(32) NOT NULL,
    `password_iterations` INT UNSIGNED NOT NULL DEFAULT 120000,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_users_username` (`username`),
    UNIQUE KEY `uk_users_email` (`email`)
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 用户头像二进制，独立于用户资料。
CREATE TABLE IF NOT EXISTS `user_avatars` (
    `uid` INT UNSIGNED NOT NULL,
    `image` MEDIUMBLOB NOT NULL,
    PRIMARY KEY (`uid`),
    CONSTRAINT `fk_user_avatars_uid` FOREIGN KEY (`uid`) REFERENCES `users` (`id`)
        ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 好友申请；标签保存为 JSON 数组文本。
CREATE TABLE IF NOT EXISTS `friend_apply` (
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `from_uid` INT UNSIGNED NOT NULL,
    `to_uid` INT UNSIGNED NOT NULL,
    `descs` VARCHAR(255) NOT NULL DEFAULT '',
    `back_name` VARCHAR(64) NOT NULL DEFAULT '',
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `tags` TEXT NULL,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_friend_apply_pair` (`from_uid`, `to_uid`),
    KEY `idx_friend_apply_to_page` (`to_uid`, `id`),
    CONSTRAINT `fk_friend_apply_from_user`
        FOREIGN KEY (`from_uid`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_apply_to_user`
        FOREIGN KEY (`to_uid`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 双向好友关系，业务层分别写入 A->B 与 B->A。
CREATE TABLE IF NOT EXISTS `friend` (
    `self_id` INT UNSIGNED NOT NULL,
    `friend_id` INT UNSIGNED NOT NULL,
    `back` VARCHAR(64) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `tags` TEXT NULL,
    PRIMARY KEY (`self_id`, `friend_id`),
    KEY `idx_friend_friend_id` (`friend_id`),
    CONSTRAINT `fk_friend_self_user`
        FOREIGN KEY (`self_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_target_user`
        FOREIGN KEY (`friend_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 会话主表。
CREATE TABLE IF NOT EXISTS `chat_thread` (
    `thread_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `type` VARCHAR(16) NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`thread_id`),
    KEY `idx_chat_thread_type` (`type`)
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 私聊会话；user1_id 为较小 UID，user2_id 为较大 UID。
CREATE TABLE IF NOT EXISTS `private_chat` (
    `thread_id` INT UNSIGNED NOT NULL,
    `user1_id` INT UNSIGNED NOT NULL,
    `user2_id` INT UNSIGNED NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`thread_id`),
    UNIQUE KEY `uk_private_chat_users` (`user1_id`, `user2_id`),
    KEY `idx_private_chat_user2` (`user2_id`, `thread_id`),
    CONSTRAINT `fk_private_chat_thread`
        FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_private_chat_user1`
        FOREIGN KEY (`user1_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_private_chat_user2`
        FOREIGN KEY (`user2_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 群聊成员关系。
CREATE TABLE IF NOT EXISTS `group_chat_member` (
    `thread_id` INT UNSIGNED NOT NULL,
    `user_id` INT UNSIGNED NOT NULL,
    `joined_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`thread_id`, `user_id`),
    KEY `idx_group_member_user_thread` (`user_id`, `thread_id`),
    CONSTRAINT `fk_group_member_thread`
        FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_group_member_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

-- 聊天消息，包含图片元数据、游标分页和消息重试查询索引。
-- 空 unique_id 为系统消息；派生 NULL 不参与幂等唯一性约束。
-- 保留原重试查询索引，业务事务和行锁继续负责重试回执及内容冲突检查。
CREATE TABLE IF NOT EXISTS `chat_message` (
    `message_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `thread_id` INT UNSIGNED NOT NULL,
    `sender_id` INT UNSIGNED NOT NULL,
    `recv_id` INT UNSIGNED NOT NULL,
    `unique_id` VARCHAR(64) NOT NULL DEFAULT '',
    `content` TEXT NOT NULL,
    `msg_type` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `media_size` BIGINT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `dedup_unique_id` VARCHAR(64) GENERATED ALWAYS AS
        (CASE WHEN OCTET_LENGTH(`unique_id`) = 0 THEN NULL ELSE `unique_id` END) VIRTUAL,
    PRIMARY KEY (`message_id`),
    KEY `idx_chat_message_thread_page` (`thread_id`, `message_id`),
    KEY `idx_chat_message_sender` (`sender_id`),
    KEY `idx_chat_message_receiver` (`recv_id`),
    KEY `idx_chat_message_retry` (`thread_id`, `sender_id`, `unique_id`),
    UNIQUE KEY `uk_chat_message_idempotency` (`thread_id`, `sender_id`, `dedup_unique_id`),
    CONSTRAINT `fk_chat_message_thread`
        FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_message_sender`
        FOREIGN KEY (`sender_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_message_receiver`
        FOREIGN KEY (`recv_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;

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
