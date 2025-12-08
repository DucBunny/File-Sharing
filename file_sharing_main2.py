import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog
import threading
import json
import os
import ctypes
import sys
import time
import hashlib
import datetime

# --- CẤU HÌNH LOAD THƯ VIỆN C ---
lib_name = "./network_lib.so"
if os.name == 'nt':
    lib_name = "./network_lib.dll"

try:
    c_net = ctypes.CDLL(lib_name)
except OSError:
    print(f"LỖI: Không tìm thấy thư viện '{lib_name}'. Hãy biên dịch code C trước.")
    sys.exit(1)

# Định nghĩa hàm C
c_net.create_server_socket.argtypes = [ctypes.c_char_p, ctypes.c_int]
c_net.create_server_socket.restype = ctypes.c_int
c_net.accept_client.argtypes = [ctypes.c_int, ctypes.c_char_p]
c_net.accept_client.restype = ctypes.c_int
c_net.connect_to_server.argtypes = [ctypes.c_char_p, ctypes.c_int]
c_net.connect_to_server.restype = ctypes.c_int
c_net.send_packet.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
c_net.send_packet.restype = ctypes.c_int
c_net.recv_packet.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
c_net.recv_packet.restype = ctypes.POINTER(ctypes.c_char)
c_net.free_mem.argtypes = [ctypes.POINTER(ctypes.c_char)]
c_net.close_socket.argtypes = [ctypes.c_int]

# Fix lỗi init_network trên Linux
if os.name == 'nt':
    try:
        c_net.init_network()
    except AttributeError:
        pass

# Wrapper functions
def c_send_json(sock_fd, data_dict):
    try:
        json_str = json.dumps(data_dict, default=str)
        data_bytes = json_str.encode('utf-8')
        return c_net.send_packet(sock_fd, data_bytes, len(data_bytes)) != -1
    except Exception as e:
        print(f"Error sending JSON: {e}")
        return False

def c_send_bytes(sock_fd, data_bytes):
    return c_net.send_packet(sock_fd, data_bytes, len(data_bytes)) != -1

def c_recv_json(sock_fd):
    out_len = ctypes.c_int(0)
    ptr = c_net.recv_packet(sock_fd, ctypes.byref(out_len))
    if not ptr or out_len.value == -1: return None
    try:
        data = ctypes.string_at(ptr, out_len.value)
        return json.loads(data.decode('utf-8'))
    finally:
        c_net.free_mem(ptr)

def c_recv_bytes(sock_fd):
    out_len = ctypes.c_int(0)
    ptr = c_net.recv_packet(sock_fd, ctypes.byref(out_len))
    if not ptr or out_len.value == -1: return None
    try:
        return ctypes.string_at(ptr, out_len.value)
    finally:
        c_net.free_mem(ptr)

# --- CẤU HÌNH DATABASE & SERVER ---
HOST = '127.0.0.1'
PORT = 65432
SERVER_ROOT = "server_storage" 

try:
    import mysql.connector
    HAS_DB = True
except ImportError:
    HAS_DB = False
    print("CẢNH BÁO: Chưa cài đặt thư viện 'mysql-connector-python'.")

# --- DATABASE MANAGER ---
class DBManager:
    def __init__(self):
        self.conn = None
        if HAS_DB:
            try:
                self.conn = mysql.connector.connect(
                    host="localhost",
                    user="fileuser",        
                    password="FilePassword123", 
                    database="file_system_db"
                )
                print("[DB] Kết nối MySQL thành công.")
            except Exception as e:
                print(f"[DB] Lỗi kết nối: {e}")
                self.conn = None

    def get_cursor(self):
        # Cần reset connection nếu bị time out (reconnect logic đơn giản)
        if self.conn and not self.conn.is_connected():
            try: self.conn.reconnect(attempts=3, delay=1)
            except: pass
        return self.conn.cursor(dictionary=True) if self.conn and self.conn.is_connected() else None

    def login(self, username, password):
        if not self.conn: return None
        cursor = self.get_cursor()
        pass_hash = hashlib.sha256(password.encode()).hexdigest()
        sql = "SELECT * FROM users WHERE username = %s AND password_hash = %s"
        cursor.execute(sql, (username, pass_hash))
        user = cursor.fetchone()
        cursor.close()
        return user

    # --- MỚI: HÀM ĐĂNG KÝ USER ---
    def register_user(self, username, password, fullname, email):
        if not self.conn: return False, "Lỗi kết nối CSDL"
        cursor = self.get_cursor()
        try:
            # Hash mật khẩu
            pass_hash = hashlib.sha256(password.encode()).hexdigest()
            sql = "INSERT INTO users (username, password_hash, full_name, email, role) VALUES (%s, %s, %s, %s, 'user')"
            cursor.execute(sql, (username, pass_hash, fullname, email))
            self.conn.commit()
            cursor.close()
            return True, "Đăng ký thành công!"
        except mysql.connector.Error as err:
            # Mã lỗi 1062 là Duplicate entry (Trùng username)
            if err.errno == 1062:
                return False, "Tên đăng nhập đã tồn tại."
            return False, f"Lỗi DB: {err}"

    def get_nodes(self, user_id, parent_id=None):
        if not self.conn: return []
        cursor = self.get_cursor()
        
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

    def create_node(self, owner_id, name, type, parent_id=None, size=0):
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

    def share_node(self, node_id, target_username, permission='read'):
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

# --- SERVER LOGIC ---
class FileServer:
    def __init__(self):
        self.db = DBManager()
        self.server_fd = -1
        if not os.path.exists(SERVER_ROOT):
            os.makedirs(SERVER_ROOT)

    def start(self):
        self.server_fd = c_net.create_server_socket(HOST.encode('utf-8'), PORT)
        if self.server_fd < 0:
            print("[-] Server khởi tạo thất bại.")
            return

        print(f"[*] Server running on {HOST}:{PORT}")
        while True:
            client_ip = ctypes.create_string_buffer(50)
            client_fd = c_net.accept_client(self.server_fd, client_ip)
            if client_fd >= 0:
                t = threading.Thread(target=self.handle_client, args=(client_fd,))
                t.start()

    def handle_client(self, client_fd):
        current_user = None 
        try:
            while True:
                req = c_recv_json(client_fd)
                if not req: break
                
                cmd = req.get('command')
                res = {"status": "error", "message": "Unknown"}

                if cmd == 'LOGIN':
                    user = self.db.login(req['username'], req['password'])
                    if user:
                        current_user = user
                        res = {"status": "success", "message": f"Xin chào {user['full_name']}", "user_id": user['id']}
                    else:
                        res = {"status": "fail", "message": "Sai tên đăng nhập hoặc mật khẩu"}

                # --- MỚI: XỬ LÝ REGISTER ---
                elif cmd == 'REGISTER':
                    ok, msg = self.db.register_user(req['username'], req['password'], req['fullname'], req['email'])
                    res = {"status": "success" if ok else "fail", "message": msg}

                elif cmd == 'LIST_NODES':
                    if current_user:
                        nodes = self.db.get_nodes(current_user['id'], req.get('parent_id'))
                        res = {"status": "success", "data": nodes}
                    else:
                        res = {"status": "fail", "message": "Chưa đăng nhập"}

                elif cmd == 'CREATE_FOLDER':
                    if current_user:
                        node_id = self.db.create_node(current_user['id'], req['name'], 'folder', req.get('parent_id'))
                        res = {"status": "success", "message": "Tạo thư mục thành công", "id": node_id}

                elif cmd == 'UPLOAD_INIT':
                    if current_user:
                        node_id = self.db.create_node(current_user['id'], req['filename'], 'file', req.get('parent_id'), req['filesize'])
                        phy_path = os.path.join(SERVER_ROOT, str(node_id))
                        c_send_json(client_fd, {"status": "ready"})
                        file_data = c_recv_bytes(client_fd)
                        if file_data is not None:
                            with open(phy_path, 'wb') as f:
                                f.write(file_data)
                            res = {"status": "success", "message": "Upload thành công"}
                        else:
                            res = {"status": "error", "message": "Lỗi truyền file"}
                
                elif cmd == 'DOWNLOAD_INIT':
                    node_id = req['node_id']
                    phy_path = os.path.join(SERVER_ROOT, str(node_id))
                    if os.path.exists(phy_path):
                        size = os.path.getsize(phy_path)
                        c_send_json(client_fd, {"status": "ready", "filesize": size})
                        with open(phy_path, 'rb') as f:
                            data = f.read()
                        c_send_bytes(client_fd, data)
                        continue
                    else:
                        res = {"status": "error", "message": "File hỏng hoặc không tồn tại"}

                elif cmd == 'SHARE_NODE':
                    if current_user:
                        ok = self.db.share_node(req['node_id'], req['target_username'], req['permission'])
                        if ok: res = {"status": "success", "message": "Đã chia sẻ thành công"}
                        else: res = {"status": "fail", "message": "Người dùng không tồn tại"}

                c_send_json(client_fd, res)
        except Exception as e:
            print(f"Server Error: {e}")
        finally:
            c_net.close_socket(client_fd)

# --- CLIENT GUI ---
class DriveGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("My Drive System")
        self.root.geometry("1000x700")
        
        self.sock_fd = -1
        self.current_user_id = None
        self.current_parent_id = None
        self.path_stack = [] 
        
        self.setup_login()

    def connect(self):
        if self.sock_fd == -1:
            self.sock_fd = c_net.connect_to_server(HOST.encode('utf-8'), PORT)
            return self.sock_fd >= 0
        return True

    def send_req(self, data):
        if not self.connect(): 
            messagebox.showerror("Lỗi", "Mất kết nối server")
            return None
        if c_send_json(self.sock_fd, data):
            return c_recv_json(self.sock_fd)
        return None

    # --- UI: LOGIN ---
    def setup_login(self):
        self.clear_ui()
        frame = tk.Frame(self.root, bg="#f0f2f5")
        frame.pack(fill=tk.BOTH, expand=True)
        
        card = tk.Frame(frame, bg="white", padx=40, pady=40, relief=tk.RAISED, borderwidth=1)
        card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        
        tk.Label(card, text="Đăng Nhập Hệ Thống", font=("Segoe UI", 20, "bold"), bg="white", fg="#1a73e8").pack(pady=(0, 20))
        
        tk.Label(card, text="Username", bg="white", anchor="w").pack(fill=tk.X)
        self.entry_user = tk.Entry(card, font=("Segoe UI", 12))
        self.entry_user.pack(fill=tk.X, pady=(0, 10))
        
        tk.Label(card, text="Password", bg="white", anchor="w").pack(fill=tk.X)
        self.entry_pass = tk.Entry(card, font=("Segoe UI", 12), show="*")
        self.entry_pass.pack(fill=tk.X, pady=(0, 20))
        
        tk.Button(card, text="Đăng Nhập", font=("Segoe UI", 12), bg="#1a73e8", fg="white", command=self.do_login).pack(fill=tk.X, pady=5)
        
        # Nút chuyển sang Đăng Ký
        tk.Button(card, text="Chưa có tài khoản? Đăng ký ngay", font=("Segoe UI", 10), bg="white", fg="#1a73e8", 
                 bd=0, cursor="hand2", command=self.setup_register).pack(fill=tk.X, pady=10)

    # --- MỚI: UI REGISTER ---
    def setup_register(self):
        self.clear_ui()
        frame = tk.Frame(self.root, bg="#f0f2f5")
        frame.pack(fill=tk.BOTH, expand=True)
        
        card = tk.Frame(frame, bg="white", padx=40, pady=30, relief=tk.RAISED)
        card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        
        tk.Label(card, text="Đăng Ký Tài Khoản", font=("Segoe UI", 18, "bold"), bg="white", fg="#4CAF50").pack(pady=(0, 15))
        
        entries = {}
        for field in ["Tên đăng nhập", "Mật khẩu", "Họ và tên", "Email"]:
            tk.Label(card, text=field, bg="white", anchor="w").pack(fill=tk.X)
            e = tk.Entry(card, font=("Segoe UI", 11))
            if field == "Mật khẩu": e.config(show="*")
            e.pack(fill=tk.X, pady=(0, 10))
            entries[field] = e
            
        def submit_reg():
            u = entries["Tên đăng nhập"].get()
            p = entries["Mật khẩu"].get()
            fn = entries["Họ và tên"].get()
            em = entries["Email"].get()
            
            if not u or not p:
                messagebox.showerror("Lỗi", "Vui lòng điền đầy đủ thông tin")
                return
                
            res = self.send_req({"command": "REGISTER", "username": u, "password": p, "fullname": fn, "email": em})
            if res and res['status'] == 'success':
                messagebox.showinfo("Thành công", "Đăng ký thành công! Vui lòng đăng nhập.")
                self.setup_login()
            else:
                messagebox.showerror("Thất bại", res.get('message', 'Lỗi không xác định'))

        tk.Button(card, text="Đăng Ký", font=("Segoe UI", 12), bg="#4CAF50", fg="white", command=submit_reg).pack(fill=tk.X, pady=10)
        tk.Button(card, text="Quay lại Đăng nhập", font=("Segoe UI", 10), bg="white", fg="#555", 
                 bd=0, cursor="hand2", command=self.setup_login).pack(fill=tk.X)

    def do_login(self):
        u = self.entry_user.get()
        p = self.entry_pass.get()
        res = self.send_req({"command": "LOGIN", "username": u, "password": p})
        if res and res['status'] == 'success':
            self.current_user_id = res['user_id']
            self.setup_drive_ui()
            self.refresh_nodes()
        else:
            messagebox.showerror("Lỗi", res.get('message', 'Login thất bại'))

    # --- UI: MAIN DRIVE ---
    def clear_ui(self):
        for w in self.root.winfo_children(): w.destroy()

    def setup_drive_ui(self):
        self.clear_ui()
        
        # Header
        header = tk.Frame(self.root, bg="white", height=50, bd=1, relief=tk.RAISED)
        header.pack(side=tk.TOP, fill=tk.X)
        tk.Label(header, text="☁ Cloud Drive", font=("Segoe UI", 14, "bold"), bg="white", fg="#5f6368").pack(side=tk.LEFT, padx=20)
        tk.Button(header, text="Đăng xuất", bg="#ff4d4f", fg="white", command=self.setup_login).pack(side=tk.RIGHT, padx=10, pady=10)

        # Body
        body = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg="#f0f0f0")
        body.pack(fill=tk.BOTH, expand=True)
        
        # Sidebar
        sidebar = tk.Frame(body, bg="white", width=200)
        body.add(sidebar, minsize=150)
        self.create_sidebar_btn(sidebar, "📂 My Drive", lambda: self.navigate_to_root()).pack(fill=tk.X, pady=2)
        tk.Frame(sidebar, height=1, bg="#ddd").pack(fill=tk.X, pady=10)
        tk.Button(sidebar, text="+ Mới", font=("Segoe UI", 12, "bold"), bg="white", fg="#1a73e8", 
                 relief=tk.FLAT, command=self.show_create_menu).pack(pady=10, padx=20, anchor="w")

        # Content
        self.main_content = tk.Frame(body, bg="white")
        body.add(self.main_content)
        
        self.crumb_frame = tk.Frame(self.main_content, bg="white", height=40)
        self.crumb_frame.pack(fill=tk.X, padx=10, pady=5)
        
        self.canvas = tk.Canvas(self.main_content, bg="white")
        scrollbar = ttk.Scrollbar(self.main_content, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = tk.Frame(self.canvas, bg="white")
        self.scrollable_frame.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.update_breadcrumbs()

    def create_sidebar_btn(self, parent, text, cmd):
        btn = tk.Button(parent, text=text, font=("Segoe UI", 11), bg="white", bd=0, anchor="w", padx=20, command=cmd)
        return btn

    def navigate_to_root(self):
        self.current_parent_id = None
        self.path_stack = []
        self.update_breadcrumbs()
        self.refresh_nodes()

    def enter_folder(self, node_id, name):
        self.path_stack.append((node_id, name))
        self.current_parent_id = node_id
        self.update_breadcrumbs()
        self.refresh_nodes()
        
    def go_back_to(self, index):
        if index == -1:
            self.navigate_to_root()
        else:
            self.path_stack = self.path_stack[:index+1]
            self.current_parent_id = self.path_stack[-1][0]
            self.update_breadcrumbs()
            self.refresh_nodes()

    def update_breadcrumbs(self):
        for w in self.crumb_frame.winfo_children(): w.destroy()
        btn = tk.Button(self.crumb_frame, text="My Drive", font=("Segoe UI", 12, "bold"), bd=0, bg="white", fg="#5f6368", command=lambda: self.go_back_to(-1))
        btn.pack(side=tk.LEFT)
        for idx, (nid, name) in enumerate(self.path_stack):
            tk.Label(self.crumb_frame, text=" > ", bg="white").pack(side=tk.LEFT)
            btn = tk.Button(self.crumb_frame, text=name, font=("Segoe UI", 12), bd=0, bg="white", fg="#5f6368", command=lambda i=idx: self.go_back_to(i))
            btn.pack(side=tk.LEFT)

    def refresh_nodes(self):
        for w in self.scrollable_frame.winfo_children(): w.destroy()
        res = self.send_req({"command": "LIST_NODES", "parent_id": self.current_parent_id})
        
        if res and res['status'] == 'success':
            nodes = res.get('data', [])
            cols = 5
            for i, node in enumerate(nodes):
                r, c = divmod(i, cols)
                self.draw_node_item(node, r, c)
        else:
            print(f"[DEBUG] Error refreshing: {res}")

    def draw_node_item(self, node, row, col):
        is_folder = node['type'] == 'folder'
        icon_char = "📁" if is_folder else "📄"
        color = "#FFC107" if is_folder else "#2196F3"
        frame = tk.Frame(self.scrollable_frame, bg="white", width=120, height=120)
        frame.grid(row=row, column=col, padx=10, pady=10)
        frame.pack_propagate(False)
        
        frame.bind("<Button-1>", lambda e: self.on_node_click(node))
        frame.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        lbl_icon = tk.Label(frame, text=icon_char, font=("Arial", 30), fg=color, bg="white")
        lbl_icon.pack(expand=True)
        lbl_icon.bind("<Button-1>", lambda e: self.on_node_click(node))
        lbl_icon.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        lbl_name = tk.Label(frame, text=node['name'], font=("Segoe UI", 10), bg="white", wraplength=100)
        lbl_name.pack(side=tk.BOTTOM, pady=5)
        if node.get('is_shared'):
            tk.Label(frame, text="👥", font=("Arial", 8), bg="white", fg="gray").place(x=5, y=5)

    def on_node_click(self, node):
        if node['type'] == 'folder':
            self.enter_folder(node['id'], node['name'])
        else:
            messagebox.showinfo("File Info", f"File: {node['name']}\nSize: {node['size']} bytes\nOwner: {node['owner_name']}")

    def show_context_menu(self, event, node):
        menu = tk.Menu(self.root, tearoff=0)
        if node['type'] == 'file':
            menu.add_command(label="Download", command=lambda: self.download_node(node))
        menu.add_command(label="Chia sẻ", command=lambda: self.share_dialog(node))
        menu.post(event.x_root, event.y_root)

    def show_create_menu(self):
        menu = tk.Menu(self.root, tearoff=0)
        menu.add_command(label="Tạo Thư Mục", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file)
        x = self.root.winfo_rootx() + 50
        y = self.root.winfo_rooty() + 150
        menu.post(x, y)

    def create_folder(self):
        name = simpledialog.askstring("Mới", "Tên thư mục:")
        if name:
            res = self.send_req({"command": "CREATE_FOLDER", "name": name, "parent_id": self.current_parent_id})
            if res and res['status'] == 'success':
                self.refresh_nodes()

    # --- UPLOAD ĐÃ SỬA LỖI ---
    def upload_file(self):
        path = filedialog.askopenfilename()
        if not path: return
        fname = os.path.basename(path)
        fsize = os.path.getsize(path)
        
        res_init = self.send_req({
            "command": "UPLOAD_INIT", 
            "filename": fname, 
            "filesize": fsize,
            "parent_id": self.current_parent_id
        })
        
        if res_init and res_init['status'] == 'ready':
            with open(path, 'rb') as f:
                data = f.read()
            c_send_bytes(self.sock_fd, data)
            
            # Quan trọng: Đọc phản hồi cuối cùng
            final_res = c_recv_json(self.sock_fd)
            if final_res and final_res['status'] == 'success':
                messagebox.showinfo("Thành công", "Upload thành công")
            else:
                messagebox.showerror("Lỗi", "Lỗi server khi lưu file")

            self.refresh_nodes()

    def download_node(self, node):
        res = self.send_req({"command": "DOWNLOAD_INIT", "node_id": node['id']})
        if res and res['status'] == 'ready':
            data = c_recv_bytes(self.sock_fd)
            if data:
                save_path = filedialog.asksaveasfilename(initialfile=node['name'])
                if save_path:
                    with open(save_path, 'wb') as f:
                        f.write(data)
                    messagebox.showinfo("Done", "Download thành công")

    def share_dialog(self, node):
        target = simpledialog.askstring("Chia sẻ", "Nhập username người nhận:")
        if target:
            res = self.send_req({
                "command": "SHARE_NODE",
                "node_id": node['id'],
                "target_username": target,
                "permission": "read"
            })
            if res:
                messagebox.showinfo("Thông báo", res.get('message'))

if __name__ == "__main__":
    server = FileServer()
    t = threading.Thread(target=server.start, daemon=True)
    t.start()
    time.sleep(1)
    
    root = tk.Tk()
    app = DriveGUI(root)
    root.mainloop()