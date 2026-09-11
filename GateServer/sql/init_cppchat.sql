-- 这是 GateServer 的数据库初始化脚本，可重复执行。
-- IF NOT EXISTS 表示数据库或表已经存在时不会重复创建，也不会因此报错。

-- 创建项目专用数据库。
-- utf8mb4 可以完整保存中文、英文和 Emoji；unicode_ci 提供不区分大小写的常规比较。
CREATE DATABASE IF NOT EXISTS `cppchat`
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

-- 把后续没有写数据库前缀的 CREATE TABLE 操作切换到 cppchat 数据库。
USE `cppchat`;

-- users 保存注册成功后的用户账号信息。
CREATE TABLE IF NOT EXISTS `users`
(
    -- 用户内部编号。UNSIGNED 表示不需要负数；AUTO_INCREMENT 表示每插入一行自动加一。
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,

    -- 用户名和邮箱由注册请求提供，NOT NULL 表示这两个字段不能为空。
    `username` VARCHAR(64) NOT NULL,
    `email` VARCHAR(255) NOT NULL,

    -- ChatServer 展示用户资料时使用的字段。
    `nick` VARCHAR(64) NOT NULL DEFAULT '',
    `profile_description` VARCHAR(255) NOT NULL DEFAULT '',
    `sex` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `icon` VARCHAR(512) NOT NULL DEFAULT '',

    -- 绝不保存明文密码。
    -- password_hash 保存 32 字节哈希转成的 64 位十六进制文本；
    -- password_salt 保存 16 字节随机盐转成的 32 位十六进制文本；
    -- password_iterations 记录 PBKDF2 迭代次数，供未来登录验证使用。
    `password_hash` CHAR(64) NOT NULL,
    `password_salt` CHAR(32) NOT NULL,
    `password_iterations` INT UNSIGNED NOT NULL DEFAULT 120000,

    -- 创建时间由 MySQL 在 INSERT 时自动填写；更新时间在该行被 UPDATE 时自动刷新。
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,

    -- id 是主键，用于唯一标识用户，也是 LAST_INSERT_ID() 返回的 uid。
    PRIMARY KEY (`id`),

    -- 两个唯一索引从数据库层阻止重复用户名和重复邮箱。
    -- 并发注册时即使两个请求同时通过前置检查，最终也只有一个 INSERT 能成功。
    UNIQUE KEY `uk_users_username` (`username`),
    UNIQUE KEY `uk_users_email` (`email`)
-- InnoDB 支持事务、崩溃恢复和行级锁，是 MySQL 业务表的常用存储引擎。
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- CREATE TABLE IF NOT EXISTS 不会给已经存在的旧 users 表补字段。
-- 以下四段动态 SQL 只在字段缺失时执行 ALTER，因此整份脚本可以重复运行。
SET @ddl = IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'users' AND COLUMN_NAME = 'nick') = 0,
    'ALTER TABLE `users` ADD COLUMN `nick` VARCHAR(64) NOT NULL DEFAULT '''' AFTER `email`',
    'SELECT 1');
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @ddl = IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'users' AND COLUMN_NAME = 'profile_description') = 0,
    'ALTER TABLE `users` ADD COLUMN `profile_description` VARCHAR(255) NOT NULL DEFAULT '''' AFTER `nick`',
    'SELECT 1');
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @ddl = IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'users' AND COLUMN_NAME = 'sex') = 0,
    'ALTER TABLE `users` ADD COLUMN `sex` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `profile_description`',
    'SELECT 1');
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

SET @ddl = IF(
    (SELECT COUNT(*) FROM information_schema.COLUMNS
     WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'users' AND COLUMN_NAME = 'icon') = 0,
    'ALTER TABLE `users` ADD COLUMN `icon` VARCHAR(512) NOT NULL DEFAULT '''' AFTER `sex`',
    'SELECT 1');
PREPARE stmt FROM @ddl;
EXECUTE stmt;
DEALLOCATE PREPARE stmt;

-- 好友申请。from_uid 是申请人，to_uid 是接收人；同一方向只保留一条申请。
CREATE TABLE IF NOT EXISTS `friend_apply`
(
    `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `from_uid` INT UNSIGNED NOT NULL,
    `to_uid` INT UNSIGNED NOT NULL,
    `descs` VARCHAR(255) NOT NULL DEFAULT '',
    `back_name` VARCHAR(64) NOT NULL DEFAULT '',
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_friend_apply_pair` (`from_uid`, `to_uid`),
    KEY `idx_friend_apply_to_page` (`to_uid`, `id`),
    CONSTRAINT `fk_friend_apply_from_user`
        FOREIGN KEY (`from_uid`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_apply_to_user`
        FOREIGN KEY (`to_uid`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 双向好友关系。业务层会分别写入 A->B 和 B->A 两条记录。
CREATE TABLE IF NOT EXISTS `friend`
(
    `self_id` INT UNSIGNED NOT NULL,
    `friend_id` INT UNSIGNED NOT NULL,
    `back` VARCHAR(64) NOT NULL DEFAULT '',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`self_id`, `friend_id`),
    KEY `idx_friend_friend_id` (`friend_id`),
    CONSTRAINT `fk_friend_self_user`
        FOREIGN KEY (`self_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_friend_target_user`
        FOREIGN KEY (`friend_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 所有私聊和群聊共享一张会话主表。
CREATE TABLE IF NOT EXISTS `chat_thread`
(
    `thread_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `type` VARCHAR(16) NOT NULL,
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`thread_id`),
    KEY `idx_chat_thread_type` (`type`)
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 私聊会话。user1_id 始终保存较小 uid，user2_id 保存较大 uid。
CREATE TABLE IF NOT EXISTS `private_chat`
(
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
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 群聊成员关系；当前 ChatServer 已经会从该表合并查询用户会话列表。
CREATE TABLE IF NOT EXISTS `group_chat_member`
(
    `thread_id` INT UNSIGNED NOT NULL,
    `user_id` INT UNSIGNED NOT NULL,
    `joined_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (`thread_id`, `user_id`),
    KEY `idx_group_member_user_thread` (`user_id`, `thread_id`),
    CONSTRAINT `fk_group_member_thread`
        FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_group_member_user`
        FOREIGN KEY (`user_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;

-- 聊天消息。thread_id + message_id 索引用于按会话游标分页加载。
CREATE TABLE IF NOT EXISTS `chat_message`
(
    `message_id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `thread_id` INT UNSIGNED NOT NULL,
    `sender_id` INT UNSIGNED NOT NULL,
    `recv_id` INT UNSIGNED NOT NULL,
    `unique_id` VARCHAR(64) NOT NULL DEFAULT '',
    `content` TEXT NOT NULL,
    `created_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    `updated_at` DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    `status` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`message_id`),
    KEY `idx_chat_message_thread_page` (`thread_id`, `message_id`),
    KEY `idx_chat_message_sender` (`sender_id`),
    KEY `idx_chat_message_receiver` (`recv_id`),
    CONSTRAINT `fk_chat_message_thread`
        FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`thread_id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_message_sender`
        FOREIGN KEY (`sender_id`) REFERENCES `users` (`id`) ON DELETE CASCADE,
    CONSTRAINT `fk_chat_message_receiver`
        FOREIGN KEY (`recv_id`) REFERENCES `users` (`id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARACTER SET utf8mb4
  COLLATE utf8mb4_unicode_ci;
