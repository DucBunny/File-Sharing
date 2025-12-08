# File: db_manager.py
import mysql.connector
import hashlib
import os
import shutil

# --- CẤU HÌNH ---
SERVER_ROOT = "server_storage" 
DB_CONFIG = {
    "host": "localhost",
    "user": "fileuser",        
    "password": "FilePassword123", 
    "database": "file_system_db",
    "port": 33061 # Cổng MySQL tùy chỉnh
}

try:
    import mysql.connector
    HAS_DB = True
except ImportError:
    HAS_DB = False
    print("CẢNH BÁO: Chưa cài đặt thư viện 'mysql-connector-python'.")


class DBManager:
    def __init__(self):
        self.conn = None
        if HAS_DB:
            try:
                self.conn = mysql.connector.connect(**DB_CONFIG)
                print("[DB] Kết nối MySQL thành công.")
            except Exception as e:
                print(f"[DB] Lỗi kết nối: {e}")
                self.conn = None

    def get_cursor(self):
        """Lấy cursor mới, xử lý reconnect nếu cần."""
        if self.conn and not self.conn.is_connected():
            try: self.conn.reconnect(attempts=3, delay=1)
            except: pass
        return self.conn.cursor(dictionary=True) if self.conn and self.conn.is_connected() else None

    # --- CHỨC NĂNG NGƯỜI DÙNG ---
    def login(self, username, password):
        """Đăng nhập người dùng."""
        if not self.conn: return None
        cursor = self.get_cursor()
        pass_hash = hashlib.sha256(password.encode()).hexdigest()
        sql = "SELECT * FROM users WHERE username = %s AND password_hash = %s"
        cursor.execute(sql, (username, pass_hash))
        user = cursor.fetchone()
        cursor.close()
        return user

    def register_user(self, username, password, fullname, email):
        """Đăng ký người dùng mới."""
        if not self.conn: return False, "Lỗi kết nối CSDL"
        cursor = self.get_cursor()
        try:
            pass_hash = hashlib.sha256(password.encode()).hexdigest()
            sql = "INSERT INTO users (username, password_hash, full_name, email, role) VALUES (%s, %s, %s, %s, 'user')"
            cursor.execute(sql, (username, pass_hash, fullname, email))
            self.conn.commit()
            cursor.close()
            return True, "Đăng ký thành công!"
        except mysql.connector.Error as err:
            if err.errno == 1062:
                return False, "Tên đăng nhập đã tồn tại."
            return False, f"Lỗi DB: {err}"

    # --- CHỨC NĂNG NODE (FILE/FOLDER) ---
    def create_node(self, owner_id, name, type, parent_id=None, size=0):
        """Tạo một node mới (file hoặc folder) trong CSDL."""
        if not self.conn: return 999
        cursor = self.get_cursor()
        sql = "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%s, %s, %s, %s, %s)"
        cursor.execute(sql, (owner_id, name, type, parent_id, size))
        self.conn.commit()
        node_id = cursor.lastrowid
        cursor.execute("INSERT INTO permissions (node_id) VALUES (%s)", (node_id,))
        self.conn.commit()
        cursor.close()
        return node_id

    def get_nodes(self, user_id, parent_id=None):
        """Lấy danh sách các node con trong thư mục hiện tại hoặc các node được chia sẻ."""
        if not self.conn: return []
        cursor = self.get_cursor()
        
        # 1. Lấy node thuộc sở hữu trong thư mục hiện tại
        query = """
            SELECT n.*, u.username as owner_name 
            FROM nodes n 
            JOIN users u ON n.owner_id = u.id
            WHERE n.owner_id = %s 
        """
        params = [user_id]
        if parent_id: 
            query += " AND n.parent_id = %s"
            params.append(parent_id)
        else:
            query += " AND n.parent_id IS NULL"
            
        cursor.execute(query, tuple(params))
        nodes = cursor.fetchall()
        
        # 2. Lấy node được chia sẻ (chỉ ở thư mục gốc)
        if parent_id is None:
            query_share = """
                SELECT n.*, u.username as owner_name, sn.permission as share_perm
                FROM shared_nodes sn
                JOIN nodes n ON sn.node_id = n.id
                JOIN users u ON n.owner_id = u.id
                WHERE sn.shared_with_user = %s
            """
            cursor.execute(query_share, (user_id,))
            shared_nodes = cursor.fetchall()
            for node in shared_nodes:
                node['is_shared'] = True
            nodes.extend(shared_nodes)
            
        cursor.close()
        return nodes

    def get_node_details(self, node_id, user_id):
        """Lấy chi tiết node và kiểm tra quyền của người dùng hiện tại."""
        if not self.conn: return None
        cursor = self.get_cursor()
        query = """
            SELECT n.*, u.username as owner_name, 
                   (n.owner_id = %s) as is_owner,
                   sn.permission as share_perm
            FROM nodes n 
            JOIN users u ON n.owner_id = u.id
            LEFT JOIN shared_nodes sn ON n.id = sn.node_id AND sn.shared_with_user = %s
            WHERE n.id = %s
        """
        cursor.execute(query, (user_id, user_id, node_id))
        node = cursor.fetchone()
        cursor.close()
        
        if node:
            # Xác định quyền ghi (rename/delete/move/copy)
            if node['is_owner'] or node.get('share_perm') in ('write', 'full'):
                node['can_write'] = True
            else:
                node['can_write'] = False
                
        return node

    def check_write_permission(self, node_id, user_id):
        """Kiểm tra nhanh quyền GHI trên node."""
        node = self.get_node_details(node_id, user_id)
        return node and node.get('can_write', False)

    # --- CHỨC NĂNG THAO TÁC FILE/FOLDER ---
    def rename_node(self, node_id, new_name):
        """Đổi tên file hoặc thư mục."""
        if not self.conn: return False
        cursor = self.get_cursor()
        try:
            sql = "UPDATE nodes SET name = %s WHERE id = %s"
            cursor.execute(sql, (new_name, node_id))
            self.conn.commit()
            return True
        except Exception as e:
            print(f"DB Error renaming: {e}")
            return False

    def delete_node(self, node_id):
        """Xóa file hoặc thư mục (xóa đệ quy nhờ ON DELETE CASCADE)."""
        if not self.conn: return False
        cursor = self.get_cursor()
        try:
            sql = "DELETE FROM nodes WHERE id = %s"
            cursor.execute(sql, (node_id,))
            self.conn.commit()
            return True
        except Exception as e:
            print(f"DB Error deleting: {e}")
            return False

    def move_node(self, node_id, new_parent_id):
        """Di chuyển node sang thư mục khác (cập nhật parent_id)."""
        if not self.conn: return False
        cursor = self.get_cursor()
        try:
            sql = "UPDATE nodes SET parent_id = %s WHERE id = %s"
            cursor.execute(sql, (new_parent_id, node_id))
            self.conn.commit()
            return True
        except Exception as e:
            print(f"DB Error moving node: {e}")
            return False
            
    def copy_node_recursive(self, original_id, new_owner_id, new_parent_id):
        """Sao chép node (và con cháu) vào CSDL và sao chép file vật lý."""
        cursor = self.get_cursor()
        cursor.execute("SELECT * FROM nodes WHERE id = %s", (original_id,))
        original_node = cursor.fetchone()
        
        if not original_node:
            return None

        # 1. Tạo node mới (bản sao) trong CSDL
        new_node_id = self.create_node(
            owner_id=new_owner_id,
            name=original_node['name'],
            type=original_node['type'],
            parent_id=new_parent_id,
            size=original_node['size']
        )
        
        # 2. Xử lý file vật lý hoặc thư mục con
        if original_node['type'] == 'file':
            # Sao chép file vật lý: original_id -> new_node_id
            src_path = os.path.join(SERVER_ROOT, str(original_id))
            dest_path = os.path.join(SERVER_ROOT, str(new_node_id))
            try:
                if os.path.exists(src_path):
                    shutil.copy2(src_path, dest_path)
            except Exception as e:
                print(f"Lỗi sao chép file vật lý {src_path} sang {dest_path}: {e}")
                
        elif original_node['type'] == 'folder':
            # Nếu là thư mục, sao chép đệ quy các node con
            cursor.execute("SELECT id FROM nodes WHERE parent_id = %s", (original_id,))
            child_ids = [row['id'] for row in cursor.fetchall()]
            
            for child_id in child_ids:
                self.copy_node_recursive(child_id, new_owner_id, new_node_id)
                
        cursor.close()
        return new_node_id
        
    def share_node(self, node_id, target_username, permission='read'):
        """Thêm quyền chia sẻ cho một node."""
        if not self.conn: return False
        cursor = self.get_cursor()
        cursor.execute("SELECT id FROM users WHERE username = %s", (target_username,))
        user = cursor.fetchone()
        if not user: return False
        try:
            sql = "INSERT INTO shared_nodes (node_id, shared_with_user, permission) VALUES (%s, %s, %s)"
            cursor.execute(sql, (node_id, user['id'], permission))
            self.conn.commit()
            return True
        except:
            return False