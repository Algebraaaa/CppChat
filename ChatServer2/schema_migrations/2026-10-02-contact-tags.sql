-- Add owner-specific tag arrays. Run against ChatServer's configured MySQL schema.
-- Existing friends and applications read NULL as an empty tag array.
-- This migration is safe to run repeatedly. Do not reset the database.
SET @contact_tag_ddl = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE friend_apply ADD COLUMN tags TEXT CHARACTER SET utf8mb4 NULL', 'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'friend_apply' AND column_name = 'tags');
PREPARE contact_tag_stmt FROM @contact_tag_ddl;
EXECUTE contact_tag_stmt;
DEALLOCATE PREPARE contact_tag_stmt;
SET @contact_tag_ddl = (SELECT IF(COUNT(*) = 0,
  'ALTER TABLE friend ADD COLUMN tags TEXT CHARACTER SET utf8mb4 NULL', 'SELECT 1')
  FROM information_schema.columns
  WHERE table_schema = DATABASE() AND table_name = 'friend' AND column_name = 'tags');
PREPARE contact_tag_stmt FROM @contact_tag_ddl;
EXECUTE contact_tag_stmt;
DEALLOCATE PREPARE contact_tag_stmt;
