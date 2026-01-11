-- 1. Xóa Database cũ (nếu có) để làm sạch lỗi
DROP DATABASE IF EXISTS file_system_db;

-- 2. Tạo lại Database
CREATE DATABASE file_system_db;
USE file_system_db;

-- 3. Tạo User riêng cho ứng dụng
CREATE USER IF NOT EXISTS 'fileuser'@'localhost' IDENTIFIED BY 'FilePassword123';
GRANT ALL PRIVILEGES ON file_system_db.* TO 'fileuser'@'localhost';
FLUSH PRIVILEGES;

-- 4. Tạo bảng Users
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    full_name VARCHAR(100),
    role VARCHAR(10) DEFAULT 'user',
    email VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 5. Tạo bảng Nodes (File/Folder)
CREATE TABLE nodes (
    id INT AUTO_INCREMENT PRIMARY KEY,
    parent_id INT,
    owner_id INT NOT NULL,
    name VARCHAR(255) NOT NULL,
    type VARCHAR(10) NOT NULL, -- 'file' hoặc 'folder'
    size BIGINT DEFAULT 0,
    path VARCHAR(500),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    FOREIGN KEY (owner_id) REFERENCES users(id) ON DELETE CASCADE,
    FOREIGN KEY (parent_id) REFERENCES nodes(id) ON DELETE CASCADE
);

-- 6. Tạo bảng Chia sẻ (Shared) - ĐÃ SỬA LỖI TẠI ĐÂY
CREATE TABLE shared_nodes (
    id INT AUTO_INCREMENT PRIMARY KEY,
    node_id INT NOT NULL,
    shared_with_user INT NOT NULL,
    permission VARCHAR(100) DEFAULT 'read', -- Tăng kích thước để chứa 'read,write,share'
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE,
    FOREIGN KEY (shared_with_user) REFERENCES users(id) ON DELETE CASCADE,
    -- Thêm ràng buộc duy nhất để hỗ trợ ON DUPLICATE KEY UPDATE
    UNIQUE KEY unique_share (node_id, shared_with_user)
);

-- 7. Tạo bảng Permissions (Linux style simulation - Optional)
CREATE TABLE permissions (
    node_id INT PRIMARY KEY,
    owner_permission CHAR(3) DEFAULT 'rwx',
    other_permission CHAR(3) DEFAULT 'r--',
    FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE
);

-- 8. Tạo dữ liệu mẫu
-- Pass '123456' (SHA256 hash)
INSERT IGNORE INTO users (username, password_hash, full_name, role) VALUES 
('admin', '8d969eef6ecad3c29a3a629280e686cf0c3f5d5a86aff3ca12020c923adc6c92', 'System Admin', 'admin');