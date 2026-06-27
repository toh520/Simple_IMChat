-- 创建聊天数据库
CREATE DATABASE IF NOT EXISTS `chat_db` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `chat_db`;

-- 1. 用户信息表
CREATE TABLE IF NOT EXISTS User (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(50) NOT NULL UNIQUE,
    password VARCHAR(50) NOT NULL,
    state VARCHAR(20) DEFAULT 'offline'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 2. 离线消息暂存表（传统兼容）
CREATE TABLE IF NOT EXISTS OfflineMessage (
    userid INT NOT NULL,
    message VARCHAR(500) NOT NULL,
    KEY (userid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 3. 好友关系表
CREATE TABLE IF NOT EXISTS Friend (
    userid INT NOT NULL,
    friendid INT NOT NULL,
    PRIMARY KEY (userid, friendid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 4. 好友申请表
CREATE TABLE IF NOT EXISTS FriendRequest (
    id INT AUTO_INCREMENT PRIMARY KEY,
    from_id INT NOT NULL,
    to_id INT NOT NULL,
    status VARCHAR(20) DEFAULT 'pending',
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_to_status (to_id, status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- 5. 消息历史全量表（Timeline 模型）
CREATE TABLE IF NOT EXISTS MessageHistory (
    msg_id BIGINT PRIMARY KEY,
    from_id INT NOT NULL,
    to_id INT NOT NULL,
    content VARCHAR(1000) NOT NULL,
    create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    KEY idx_to_msg (to_id, msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
