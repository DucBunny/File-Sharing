-- File này dùng để khởi tạo database và user cho hệ thống
-- Chạy bằng lệnh: sudo mysql < setup_db.sql

-- 1. Tạo Database
CREATE DATABASE IF NOT EXISTS file_system_db;
USE file_system_db;

-- 2. Tạo User riêng cho ứng dụng (Không dùng root để bảo mật và tránh lỗi auth_socket trên Linux)
-- Lưu ý: Nếu bạn đổi password ở đây, hãy nhớ đổi trong file python (dòng 85)
CREATE USER IF NOT EXISTS 'fileuser'@'localhost' IDENTIFIED BY 'FilePassword123';
GRANT ALL PRIVILEGES ON file_system_db.* TO 'fileuser'@'localhost';
FLUSH PRIVILEGES;

-- 3. Tạo bảng Users
CREATE TABLE IF NOT EXISTS users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(50) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    full_name VARCHAR(100),
    role VARCHAR(10) DEFAULT 'user',
    email VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- 4. Tạo bảng Nodes (File/Folder)
CREATE TABLE IF NOT EXISTS nodes (
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

-- 5. Tạo bảng Chia sẻ (Shared)
CREATE TABLE IF NOT EXISTS shared_nodes (
    id INT AUTO_INCREMENT PRIMARY KEY,
    node_id INT NOT NULL,
    shared_with_user INT NOT NULL,
    permission VARCHAR(10) DEFAULT 'read', -- read, write, full
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE,
    FOREIGN KEY (shared_with_user) REFERENCES users(id) ON DELETE CASCADE
);

-- 6. Tạo bảng Permissions (Linux style simulation)
CREATE TABLE IF NOT EXISTS permissions (
    node_id INT PRIMARY KEY,
    owner_permission CHAR(3) DEFAULT 'rwx',
    other_permission CHAR(3) DEFAULT 'r--',
    FOREIGN KEY (node_id) REFERENCES nodes(id) ON DELETE CASCADE
);

-- 7. Tạo dữ liệu mẫu
-- Pass 'admin123' (SHA256 hash)
INSERT IGNORE INTO users (username, password_hash, full_name, role) VALUES 
('admin', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9', 'System Admin', 'admin'),
('user1', '240be518fabd2724ddb6f04eeb1da5967448d7e831c08c8fa822809f74c720a9', 'Nguyen Van A', 'user');