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
) ENGINE = InnoDB;
