# File: client_gui.py
import tkinter as tk
import os
from tkinter import ttk, messagebox, filedialog, simpledialog
from net_utils import c_connect_to_server, c_send_json, c_recv_json, c_recv_bytes, c_send_bytes, c_close_socket

# --- CẤU HÌNH ---
HOST = '127.0.0.1'
PORT = 65432

class DriveGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("My Drive System")
        self.root.geometry("1000x700")
        
        self.sock_fd = -1
        self.current_user_id = None
        self.current_parent_id = None
        self.path_stack = [] 

        # Clipboard cho Copy/Cut/Paste
        self.clipboard_node = None     # {id, name, type, ...}
        self.clipboard_action = None   # 'copy' hoặc 'move'
        
        self.setup_login()

    def connect(self):
        """Thiết lập kết nối TCP với server nếu chưa có."""
        if self.sock_fd == -1:
            self.sock_fd = c_connect_to_server(HOST, PORT)
            return self.sock_fd >= 0
        return True

    def send_req(self, data):
        """Gửi yêu cầu JSON và đợi phản hồi JSON từ server."""
        if not self.connect(): 
            messagebox.showerror("Lỗi", "Mất kết nối server")
            return None
        if c_send_json(self.sock_fd, data):
            return c_recv_json(self.sock_fd)
        return None

    # --- UI: LOGIN/REGISTER ---
    def clear_ui(self):
        for w in self.root.winfo_children(): w.destroy()

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
        
        tk.Button(card, text="Chưa có tài khoản? Đăng ký ngay", font=("Segoe UI", 10), bg="white", fg="#1a73e8", 
                 bd=0, cursor="hand2", command=self.setup_register).pack(fill=tk.X, pady=10)

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
            u, p, fn, em = entries["Tên đăng nhập"].get(), entries["Mật khẩu"].get(), entries["Họ và tên"].get(), entries["Email"].get()
            
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

    # --- UI: DRIVE MAIN ---
    def setup_drive_ui(self):
        self.clear_ui()
        
        header = tk.Frame(self.root, bg="white", height=50, bd=1, relief=tk.RAISED)
        header.pack(side=tk.TOP, fill=tk.X)
        tk.Label(header, text="☁ Cloud Drive", font=("Segoe UI", 14, "bold"), bg="white", fg="#5f6368").pack(side=tk.LEFT, padx=20)
        tk.Button(header, text="Đăng xuất", bg="#ff4d4f", fg="white", command=self.setup_login).pack(side=tk.RIGHT, padx=10, pady=10)

        body = tk.PanedWindow(self.root, orient=tk.HORIZONTAL, bg="#f0f0f0")
        body.pack(fill=tk.BOTH, expand=True)
        
        sidebar = tk.Frame(body, bg="white", width=200)
        body.add(sidebar, minsize=150)
        self.create_sidebar_btn(sidebar, "📂 My Drive", lambda: self.navigate_to_root()).pack(fill=tk.X, pady=2)
        tk.Frame(sidebar, height=1, bg="#ddd").pack(fill=tk.X, pady=10)
        tk.Button(sidebar, text="+ Mới", font=("Segoe UI", 12, "bold"), bg="white", fg="#1a73e8", 
                 relief=tk.FLAT, command=self.show_create_menu).pack(pady=10, padx=20, anchor="w")

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

    # --- CHỨC NĂNG DUYỆT FILE ---
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

    # --- CHỨC NĂNG THAO TÁC FILE ---
    def show_context_menu(self, event, node):
        menu = tk.Menu(self.root, tearoff=0)
        
        # Thao tác cơ bản
        menu.add_command(label="Sửa tên", command=lambda: self.rename_node_dialog(node))
        menu.add_command(label="Xóa", command=lambda: self.delete_node_action(node))
        menu.add_separator()
        
        # Thao tác Clipboard
        menu.add_command(label="Sao chép (Copy)", command=lambda: self.set_clipboard(node, 'copy'))
        menu.add_command(label="Cắt (Cut)", command=lambda: self.set_clipboard(node, 'move'))

        if self.clipboard_node:
            action_text = "Dán (Paste Copy)" if self.clipboard_action == 'copy' else "Dán (Paste Cut)"
            menu.add_command(label=action_text, command=self.paste_action)
            menu.add_separator()
        
        if node['type'] == 'file':
            menu.add_command(label="Download", command=lambda: self.download_node(node))
            
        menu.add_command(label="Chia sẻ", command=lambda: self.share_dialog(node))
        
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def set_clipboard(self, node, action):
        """Thiết lập Clipboard cho thao tác Copy/Cut."""
        self.clipboard_node = node
        self.clipboard_action = action
        messagebox.showinfo("Clipboard", f"Đã {action} node: {node['name']}")
        
    def paste_action(self):
        """Thực hiện thao tác Paste (Copy hoặc Move) lên thư mục hiện tại."""
        if not self.clipboard_node:
            messagebox.showerror("Lỗi", "Clipboard trống.")
            return

        command = "COPY_NODE" if self.clipboard_action == 'copy' else "MOVE_NODE"
        message = "Sao chép" if self.clipboard_action == 'copy' else "Di chuyển"

        node_to_paste = self.clipboard_node
        target_parent_id = self.current_parent_id
        
        req = {
            "command": command,
            "node_id": node_to_paste['id'],
            "target_parent_id": target_parent_id
        }
        
        res = self.send_req(req)
        
        if res and res['status'] == 'success':
            messagebox.showinfo("Thành công", f"{message} thành công.")
            
            if self.clipboard_action == 'move':
                self.clipboard_node = None
                self.clipboard_action = None
                
            self.refresh_nodes()
        else:
            messagebox.showerror("Thất bại", res.get('message', f"{message} thất bại."))

    def show_create_menu(self):
        menu = tk.Menu(self.root, tearoff=0)
        menu.add_command(label="Tạo Thư Mục", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file)
        
        x = self.root.winfo_rootx() + 50
        y = self.root.winfo_rooty() + 150
        
        try:
            menu.tk_popup(x, y)
        finally:
            menu.grab_release()

    def create_folder(self):
        name = simpledialog.askstring("Mới", "Tên thư mục:")
        if name:
            res = self.send_req({"command": "CREATE_FOLDER", "name": name, "parent_id": self.current_parent_id})
            if res and res['status'] == 'success':
                self.refresh_nodes()

    def rename_node_dialog(self, node):
        name = simpledialog.askstring("Đổi tên", f"Tên mới cho {node['name']}:", initialvalue=node['name'])
        if name and name != node['name']:
            res = self.send_req({"command": "RENAME_NODE", "node_id": node['id'], "new_name": name})
            if res and res['status'] == 'success':
                messagebox.showinfo("Thành công", res['message'])
                self.refresh_nodes()
            else:
                messagebox.showerror("Thất bại", res.get('message', 'Đổi tên thất bại.'))

    def delete_node_action(self, node):
        confirm_msg = f"Bạn có chắc chắn muốn xóa {'thư mục' if node['type'] == 'folder' else 'file'} **{node['name']}**?"
        if messagebox.askyesno("Xác nhận xóa", confirm_msg):
            res = self.send_req({"command": "DELETE_NODE", "node_id": node['id']})
            if res and res['status'] == 'success':
                messagebox.showinfo("Thành công", res['message'])
                self.refresh_nodes()
            else:
                messagebox.showerror("Thất bại", res.get('message', 'Xóa thất bại.'))

    # UPLOAD
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
            try:
                with open(path, 'rb') as f:
                    data = f.read()
                
                c_send_bytes(self.sock_fd, data)
                
                final_res = c_recv_json(self.sock_fd)
                if final_res and final_res['status'] == 'success':
                    messagebox.showinfo("Thành công", "Upload thành công")
                else:
                    messagebox.showerror("Lỗi", final_res.get('message', 'Lỗi server khi lưu file'))

                self.refresh_nodes()
            except Exception as e:
                messagebox.showerror("Lỗi", f"Lỗi đọc file: {e}")

    # DOWNLOAD
    def download_node(self, node):
        res = self.send_req({"command": "DOWNLOAD_INIT", "node_id": node['id']})
        if res and res['status'] == 'ready':
            data = c_recv_bytes(self.sock_fd)
            if data:
                save_path = filedialog.asksaveasfilename(initialfile=node['name'])
                if save_path:
                    try:
                        with open(save_path, 'wb') as f:
                            f.write(data)
                        messagebox.showinfo("Done", "Download thành công")
                    except Exception as e:
                        messagebox.showerror("Lỗi", f"Không thể lưu file: {e}")
            else:
                messagebox.showerror("Lỗi", "Lỗi truyền file từ server")

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