import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog
import ctypes
import os
import hashlib
# Thư viện PIL/Pillow cần thiết cho việc tải file ảnh
try:
    from PIL import Image, ImageTk
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False
    print("Thư viện Pillow không được tìm thấy. Icon sẽ dùng Unicode.")

# --- ĐỊNH NGHĨA STRUCT C TRONG PYTHON ---
class FileInfo(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_int),
        ("name", ctypes.c_char * 256),
        ("type", ctypes.c_char * 10),
        ("size", ctypes.c_longlong),
        ("owner", ctypes.c_char * 50),
        ("is_shared", ctypes.c_int)
    ]

# --- LOAD THƯ VIỆN C ---
lib_name = "./client_logic.so" if os.name != 'nt' else "./client_logic.dll"
try:
    cli = ctypes.CDLL(lib_name)
except OSError:
    print("Vui lòng biên dịch client_logic.c thành thư viện động trước!")
    exit(1)

# Khai báo kiểu dữ liệu hàm
cli.cli_connect.argtypes = [ctypes.c_char_p, ctypes.c_int]
cli.cli_connect.restype = ctypes.c_int

cli.cli_login.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_login.restype = ctypes.c_int # Returns UserID

# Đăng ký: username, pass_hash, fullname, email, out_msg
cli.cli_register.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_register.restype = ctypes.c_int

# Dùng chung cho LIST và SEARCH
cli.cli_list_files.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
cli.cli_list_files.restype = ctypes.POINTER(FileInfo)

cli.cli_search_files.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
cli.cli_search_files.restype = ctypes.POINTER(FileInfo)

cli.cli_upload.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_longlong, ctypes.c_int, ctypes.c_char_p]
cli.cli_upload.restype = ctypes.c_int

# Tạo folder: name, parent_id, out_msg
cli.cli_create_folder.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]
cli.cli_create_folder.restype = ctypes.c_int

# Download: node_id, local_filepath, out_msg
cli.cli_download.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_download.restype = ctypes.c_int

# Rename: node_id, new_name, out_msg
cli.cli_rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_rename.restype = ctypes.c_int

# Delete: node_id, out_msg
cli.cli_delete.argtypes = [ctypes.c_int, ctypes.c_char_p]
cli.cli_delete.restype = ctypes.c_int

# Copy: node_id, target_parent_id, out_msg
cli.cli_copy.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
cli.cli_copy.restype = ctypes.c_int

# Move: node_id, target_parent_id, out_msg
cli.cli_move.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
cli.cli_move.restype = ctypes.c_int

# Share: node_id, target_username, permission, out_msg (Mới)
cli.cli_share_node.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_share_node.restype = ctypes.c_int


cli.cli_free_files.argtypes = [ctypes.POINTER(FileInfo)]

# --- GUI ---
class DriveGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("C-Logic Drive Client")
        self.geometry("1000x700")
        
        # --- CLIPBOARD STATE ---
        self.clipboard_node = None
        self.clipboard_action = None
        self.is_searching = False # Trạng thái tìm kiếm (Mới)
        self.logged_in_username = None # Tên người dùng đang đăng nhập (Mới)
        
        # --- TẢI VÀ CHUẨN BỊ ICONS ---
        self.load_icons()
        
        # Kết nối ngay khi mở app
        if not cli.cli_connect(b"127.0.0.1", 65432):
            messagebox.showerror("Error", "Không thể kết nối Server C")
            self.destroy()
            return

        self.user_id = -1
        self.current_parent_id = 0
        self.path_stack = []
        self.setup_login()
        
    def load_icons(self):
        icon_path = "icons"
        self.icon_folder = None
        self.icon_file = None
        self.icon_shared = None
        self.use_image_icons = False

        if PIL_AVAILABLE:
            try:
                self.icon_folder = ImageTk.PhotoImage(
                    Image.open(os.path.join(icon_path, "folder.png")).resize((48, 48))
                )
                self.icon_file = ImageTk.PhotoImage(
                    Image.open(os.path.join(icon_path, "file.png")).resize((48, 48))
                )
                self.icon_shared = ImageTk.PhotoImage(
                    Image.open(os.path.join(icon_path, "shared.png")).resize((16, 16))
                )
                print("Đã tải ICON từ file thành công.")
                self.use_image_icons = True
            except Exception as e:
                print(f"Lỗi khi tải file ICON: {e}. Sử dụng icon Unicode.")

    # --- UI: LOGIN ---
    def setup_login(self):
        self.clear_ui()
        frame = tk.Frame(self, bg="#f0f2f5")
        frame.pack(fill=tk.BOTH, expand=True)
        
        card = tk.Frame(frame, bg="white", padx=40, pady=40, relief=tk.RAISED, borderwidth=1)
        card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        
        tk.Label(card, text="Đăng Nhập", font=("Segoe UI", 20, "bold"), bg="white", fg="#1a73e8").pack(pady=(0, 20))
        
        tk.Label(card, text="Username", bg="white", anchor="w").pack(fill=tk.X)
        user_entry = tk.Entry(card, font=("Segoe UI", 12))
        user_entry.pack(fill=tk.X, pady=(0, 10))
        
        tk.Label(card, text="Password", bg="white", anchor="w").pack(fill=tk.X)
        pass_entry = tk.Entry(card, font=("Segoe UI", 12), show="*")
        pass_entry.pack(fill=tk.X, pady=(0, 20))

        # Lưu trữ entry cho việc đăng nhập thành công
        self.login_user_entry = user_entry
        
        def login():
            u_raw = user_entry.get()
            u = u_raw.encode('utf-8')
            p = hashlib.sha256(pass_entry.get().encode()).hexdigest().encode('utf-8')
            msg_buf = ctypes.create_string_buffer(256)
            
            uid = cli.cli_login(u, p, msg_buf)
            if uid != -1:
                self.user_id = uid
                self.logged_in_username = u_raw # Lưu username khi đăng nhập thành công
                self.setup_drive_ui()
            else:
                messagebox.showerror("Login Fail", msg_buf.value.decode('utf-8'))

        tk.Button(card, text="Đăng Nhập", font=("Segoe UI", 12), bg="#1a73e8", fg="white", command=login).pack(fill=tk.X, pady=10)
        
        tk.Button(card, text="Chưa có tài khoản? Đăng ký ngay", font=("Segoe UI", 10), bg="white", fg="#1a73e8", 
                 bd=0, cursor="hand2", command=self.setup_register).pack(fill=tk.X, pady=10)

    # --- UI: REGISTER ---
    def setup_register(self):
        self.clear_ui()
        frame = tk.Frame(self, bg="#f0f2f5")
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
            u = entries["Tên đăng nhập"].get().encode('utf-8')
            p_raw = entries["Mật khẩu"].get()
            p = hashlib.sha256(p_raw.encode()).hexdigest().encode('utf-8')
            fn = entries["Họ và tên"].get().encode('utf-8')
            em = entries["Email"].get().encode('utf-8')
            
            if not u or not p_raw:
                messagebox.showerror("Lỗi", "Vui lòng điền đầy đủ thông tin")
                return
                
            msg_buf = ctypes.create_string_buffer(256)
            res = cli.cli_register(u, p, fn, em, msg_buf)
            
            msg_str = msg_buf.value.decode('utf-8', errors='ignore')
            if res:
                messagebox.showinfo("Thành công", "Đăng ký thành công! Vui lòng đăng nhập.")
                self.setup_login()
            else:
                messagebox.showerror("Thất bại", msg_str)

        tk.Button(card, text="Đăng Ký", font=("Segoe UI", 12), bg="#4CAF50", fg="white", command=submit_reg).pack(fill=tk.X, pady=10)
        tk.Button(card, text="Quay lại Đăng nhập", font=("Segoe UI", 10), bg="white", fg="#555", 
                 bd=0, cursor="hand2", command=self.setup_login).pack(fill=tk.X)

    # --- UI: MAIN DRIVE ---
    def setup_drive_ui(self):
        self.clear_ui()
        
        # Header Bar
        header = tk.Frame(self, bg="white", height=50, bd=1, relief=tk.RAISED)
        header.pack(side=tk.TOP, fill=tk.X)
        tk.Label(header, text="Cloud Drive", font=("Segoe UI", 14, "bold"), bg="white", fg="#5f6368").pack(side=tk.LEFT, padx=20)
        
        # Thanh tìm kiếm (Mới)
        search_frame = tk.Frame(header, bg="white")
        self.search_entry = tk.Entry(search_frame, font=("Segoe UI", 11), width=30)
        self.search_entry.pack(side=tk.LEFT, padx=5, pady=10)
        tk.Button(search_frame, text=" Tìm", command=self.search_action).pack(side=tk.LEFT, padx=5)
        search_frame.pack(side=tk.LEFT, padx=20)
        
        tk.Button(header, text="Đăng xuất", bg="#ff4d4f", fg="white", command=self.setup_login).pack(side=tk.RIGHT, padx=10, pady=10)

        # Body Split
        body = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#f0f0f0")
        body.pack(fill=tk.BOTH, expand=True)
        
        # Left Sidebar
        sidebar = tk.Frame(body, bg="white", width=200)
        body.add(sidebar, minsize=150)
        
        self.create_sidebar_btn(sidebar, "📂 My Drive", lambda: self.navigate_to_root()).pack(fill=tk.X, pady=2)
        tk.Frame(sidebar, height=1, bg="#ddd").pack(fill=tk.X, pady=10)
        
        # Nút "+ Mới" mở menu
        tk.Button(sidebar, text="✚ Tạo Mới", font=("Segoe UI", 12, "bold"), bg="#1a73e8", fg="white", 
                 relief=tk.FLAT, command=self.show_create_menu).pack(pady=10, padx=20, anchor="w", fill=tk.X)

        # Right Content
        self.main_content = tk.Frame(body, bg="white")
        body.add(self.main_content)
        
        # Breadcrumb/Search Status Bar
        self.crumb_frame = tk.Frame(self.main_content, bg="white", height=40)
        self.crumb_frame.pack(fill=tk.X, padx=10, pady=5)
        
        # Grid View
        self.canvas = tk.Canvas(self.main_content, bg="white")
        scrollbar = ttk.Scrollbar(self.main_content, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = tk.Frame(self.canvas, bg="white")

        self.scrollable_frame.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        
        self.canvas.bind("<Button-3>", lambda e: self.show_canvas_context_menu(e))
        self.scrollable_frame.bind("<Button-3>", lambda e: self.show_canvas_context_menu(e))
        
        self.scrollable_frame.bind("<Button-1>", lambda e: self.scrollable_frame.focus_set())
        self.canvas.bind("<Button-1>", lambda e: self.scrollable_frame.focus_set())

        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)

        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.update_breadcrumbs()
        self.refresh_nodes()
        self.update_sidebar_paste_state()

    def create_sidebar_btn(self, parent, text, cmd):
        btn = tk.Button(parent, text=text, font=("Segoe UI", 11), bg="white", bd=0, anchor="w", padx=20, command=cmd)
        return btn
    
    def update_sidebar_paste_state(self):
        pass

    def show_canvas_context_menu(self, event):
        menu = tk.Menu(self, tearoff=0)
        
        menu.add_command(label="Tạo Thư Mục Mới", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file)
        
        if self.clipboard_node:
            menu.add_separator()
            menu.add_command(label="Dán (Paste)", command=self.paste_node)
        
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def show_create_menu(self):
        menu = tk.Menu(self, tearoff=0)
        menu.add_command(label="Tạo Thư Mục Mới", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file)
        
        if self.clipboard_node:
            menu.add_separator()
            menu.add_command(label="Dán (Paste)", command=self.paste_node)
        
        x = self.winfo_rootx() + 50
        y = self.winfo_rooty() + 150
        
        try:
            menu.tk_popup(x, y)
        finally:
            menu.grab_release()

    # --- ACTIONS ---
    def execute_cli_action(self, cli_func, *args, success_msg="Thành công"):
        msg_buf = ctypes.create_string_buffer(256)
        c_args = []
        for arg in args:
            if isinstance(arg, str):
                c_args.append(arg.encode('utf-8'))
            else:
                c_args.append(arg)
        
        res = cli_func(*c_args, msg_buf)
        msg_str = msg_buf.value.decode('utf-8', errors='ignore')
        
        if res:
            messagebox.showinfo(success_msg, msg_str)
            self.refresh_nodes()
            return True
        else:
            messagebox.showerror("Thất bại", msg_str)
            return False

    def search_action(self):
        query = self.search_entry.get().strip()
        if query:
            self.is_searching = True
            self.search_query = query
            self.update_breadcrumbs()
            self.refresh_nodes(search_mode=True)
        else:
            # Nếu người dùng nhấn tìm kiếm khi ô trống, thoát chế độ tìm kiếm
            if self.is_searching:
                self.navigate_to_root() # Thoát tìm kiếm và về root
            else:
                 messagebox.showinfo("Thông báo", "Vui lòng nhập từ khóa để tìm kiếm.")


    def share_node(self, node):
        # ĐÃ XÓA CHECK LỖI TẠI PYTHON - Tin tưởng Server C
        
        target_username = simpledialog.askstring("Chia sẻ", f"Chia sẻ '{node['name']}' với người dùng nào?")
        if not target_username: return

        permission = simpledialog.askstring("Quyền", "Nhập quyền (read/write):", initialvalue="read")
        if permission not in ["read", "write"]:
            messagebox.showerror("Lỗi", "Quyền không hợp lệ. Chỉ cho phép 'read' hoặc 'write'.")
            return

        self.execute_cli_action(cli.cli_share_node, node['id'], target_username, permission, 
                                success_msg=f"Chia sẻ '{node['name']}' thành công")
        
    def get_current_username(self):
        # ĐÃ SỬA: Lấy username thực tế
        return self.logged_in_username if self.logged_in_username else "Unknown" 


    def create_folder(self):
        name = simpledialog.askstring("Tạo Thư Mục", "Tên thư mục:")
        if name:
            self.execute_cli_action(cli.cli_create_folder, name, self.current_parent_id, 
                                    success_msg="Tạo thư mục thành công")

    def upload_file(self):
        path = filedialog.askopenfilename()
        if not path: return
        fname = os.path.basename(path)
        fpath = path
        fsize = os.path.getsize(path)
        
        self.execute_cli_action(cli.cli_upload, fpath, fname, fsize, self.current_parent_id, 
                                success_msg="Tải lên thành công")

    def download_file(self, node):
        if node['type'] == 'folder':
            messagebox.showwarning("Cảnh báo", "Tải xuống thư mục (dạng Zip) chưa được hỗ trợ hoàn toàn.")
            return

        save_path = filedialog.asksaveasfilename(initialfile=node['name'], 
                                                  defaultextension="")
        if not save_path: return
        
        self.execute_cli_action(cli.cli_download, node['id'], save_path, 
                                success_msg=f"Tải xuống '{node['name']}' thành công")

    def rename_node(self, node):
        new_name = simpledialog.askstring("Đổi tên", f"Đổi tên '{node['name']}' thành:", initialvalue=node['name'])
        if new_name and new_name != node['name']:
            self.execute_cli_action(cli.cli_rename, node['id'], new_name, 
                                    success_msg="Đổi tên thành công")

    def delete_node(self, node):
        confirm = messagebox.askyesno("Xác nhận Xóa", f"Bạn có chắc chắn muốn xóa '{node['name']}' không? Hành động này không thể hoàn tác.")
        if confirm:
            self.execute_cli_action(cli.cli_delete, node['id'], 
                                    success_msg="Xóa thành công")

    def copy_node_to_clipboard(self, node):
        self.clipboard_node = node
        self.clipboard_action = 'copy'
        messagebox.showinfo("Clipboard", f"Đã Copy '{node['name']}'. Chọn Paste để dán.")
        self.update_sidebar_paste_state()

    def cut_node_to_clipboard(self, node):
        self.clipboard_node = node
        self.clipboard_action = 'move'
        messagebox.showinfo("Clipboard", f"Đã Cut '{node['name']}'. Chọn Paste để di chuyển.")
        self.update_sidebar_paste_state()
        
    def paste_node(self):
        if not self.clipboard_node:
            messagebox.showerror("Lỗi", "Clipboard trống.")
            return

        source_node = self.clipboard_node
        target_id = self.current_parent_id
        
        if source_node['id'] == target_id:
             messagebox.showerror("Lỗi", "Không thể dán (Copy/Move) file/folder vào chính nó.")
             return

        if self.clipboard_action == 'copy':
            cli_func = cli.cli_copy
            success_msg = "Sao chép thành công"
        elif self.clipboard_action == 'move':
            cli_func = cli.cli_move
            success_msg = "Di chuyển thành công"
        else:
            return

        if self.execute_cli_action(cli_func, source_node['id'], target_id, success_msg=success_msg):
            if self.clipboard_action == 'move':
                self.clipboard_node = None
                self.clipboard_action = None
        
        self.update_sidebar_paste_state()

    # --- NAVIGATION LOGIC ---
    def navigate_to_root(self):
        # SỬA LỖI KẸT TÌM KIẾM
        self.current_parent_id = 0
        self.path_stack = []
        self.is_searching = False
        self.search_entry.delete(0, tk.END)
        self.update_breadcrumbs()
        self.refresh_nodes()

    def enter_folder(self, node_id, name):
        # SỬA LỖI KẸT TÌM KIẾM
        if self.is_searching:
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
        
        self.path_stack.append((node_id, name))
        self.current_parent_id = node_id
        self.update_breadcrumbs()
        self.refresh_nodes()
        
    def go_back_to(self, index):
        # SỬA LỖI KẸT TÌM KIẾM
        if index == -1: # Root
            self.navigate_to_root()
        else:
            self.path_stack = self.path_stack[:index+1]
            self.current_parent_id = self.path_stack[-1][0]
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
            self.update_breadcrumbs()
            self.refresh_nodes()

    def update_breadcrumbs(self):
        for w in self.crumb_frame.winfo_children(): w.destroy()
        
        if self.is_searching:
            tk.Label(self.crumb_frame, text=f" Kết quả tìm kiếm cho: '{self.search_query}'", 
                     font=("Segoe UI", 12, "bold"), bg="white", fg="#E91E63").pack(side=tk.LEFT)
        else:
            btn = tk.Button(self.crumb_frame, text="My Drive", font=("Segoe UI", 12, "bold"), bd=0, bg="white", 
                        fg="#5f6368", command=lambda: self.go_back_to(-1))
            btn.pack(side=tk.LEFT)
            
            for idx, (nid, name) in enumerate(self.path_stack):
                tk.Label(self.crumb_frame, text=" > ", bg="white").pack(side=tk.LEFT)
                btn = tk.Button(self.crumb_frame, text=name, font=("Segoe UI", 12), bd=0, bg="white", 
                            fg="#5f6368", command=lambda i=idx: self.go_back_to(i))
                btn.pack(side=tk.LEFT)

    # --- DATA FETCHING (C LOGIC) ---
    def refresh_nodes(self, search_mode=False):
        for w in self.scrollable_frame.winfo_children(): w.destroy()
        
        count = ctypes.c_int(0)

        if search_mode:
            files_ptr = cli.cli_search_files(self.search_query.encode('utf-8'), ctypes.byref(count))
        else:
            files_ptr = cli.cli_list_files(self.current_parent_id, ctypes.byref(count))

        if count.value > 0 and files_ptr:
            cols = 5
            for i in range(count.value):
                f = files_ptr[i]
                
                node_data = {
                    'id': f.id,
                    'name': f.name.decode('utf-8', errors='ignore'),
                    'type': f.type.decode('utf-8', errors='ignore'),
                    'size': f.size,
                    'owner': f.owner.decode('utf-8', errors='ignore'),
                    'is_shared': f.is_shared
                }
                
                r, c = divmod(i, cols)
                self.draw_node_item(node_data, r, c)
            
            cli.cli_free_files(files_ptr)
        elif search_mode:
             tk.Label(self.scrollable_frame, text=f"Không tìm thấy kết quả nào cho '{self.search_query}'", font=("Segoe UI", 14), fg="gray", bg="white").pack(padx=20, pady=50)

    def draw_node_item(self, node, row, col):
        is_folder = (node['type'] == 'folder')
        
        icon_img = None
        icon_char = ""
        color = ""

        if self.use_image_icons:
            icon_img = self.icon_folder if is_folder else self.icon_file
        else:
            # Fallback Unicode Icons
            if is_folder:
                icon_char = "🗀"
                color = "#FFC107"
            else:
                icon_char = "🗋"
                color = "#4285F4" 
        
        # Container
        frame = tk.Frame(self.scrollable_frame, bg="white", width=120, height=120, padx=5, pady=5)
        
        if self.clipboard_node and self.clipboard_action == 'move' and self.clipboard_node['id'] == node['id']:
             frame.config(bg="#f0f0f0", relief=tk.GROOVE)

        frame.grid(row=row, column=col, padx=10, pady=10)
        frame.pack_propagate(False)
        
        # --- BIND SỰ KIỆN VÀO FRAME ---
        frame.bind("<Double-1>", lambda e: self.on_node_double_click(node))
        frame.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        # --- ICON ---
        if icon_img:
            lbl_icon = tk.Label(frame, image=icon_img, bg="white")
            lbl_icon.image = icon_img
            lbl_icon.pack(expand=True)
        else:
            lbl_icon = tk.Label(frame, text=icon_char, font=("Arial", 40), fg=color, bg="white")
            lbl_icon.pack(expand=True)
        
        # --- NAME ---
        lbl_name = tk.Label(
            frame,
            text=node['name'],
            font=("Segoe UI", 10),
            bg="white",
            wraplength=100
        )
        lbl_name.pack(side=tk.BOTTOM, pady=5)
        
        # --- SHARED ICON ---
        if node.get('is_shared'):
            if self.icon_shared and self.use_image_icons:
                 lbl_shared = tk.Label(frame, image=self.icon_shared, bg="white")
                 lbl_shared.image = self.icon_shared
                 lbl_shared.place(x=5, y=5)
            else:
                 tk.Label(frame, text="👥", font=("Arial", 8), bg="white", fg="gray").place(x=5, y=5)
            
        # Bind các widget con vào sự kiện Double-Click và chuột phải của Frame
        for widget in [lbl_icon, lbl_name]:
            widget.bind("<Double-1>", lambda e: self.on_node_double_click(node))
            widget.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
            widget.bind("<Button-1>", lambda e: None) 

    def on_node_double_click(self, node):
        if node['type'] == 'folder' and not self.is_searching:
            self.enter_folder(node['id'], node['name'])
        elif self.is_searching:
            messagebox.showinfo("Lưu ý", "Vui lòng xóa tìm kiếm hoặc điều hướng để xem nội dung thư mục.")
        else:
            messagebox.showinfo("File Info", f"File: {node['name']}\nSize: {node['size']} bytes\nOwner: {node['owner']}")

    def show_context_menu(self, event, node):
        menu = tk.Menu(self, tearoff=0)
        
        menu.add_command(label="Đổi tên", command=lambda: self.rename_node(node))
        menu.add_command(label="Sao chép (Copy)", command=lambda: self.copy_node_to_clipboard(node))
        menu.add_command(label="Cắt (Cut) / Di chuyển", command=lambda: self.cut_node_to_clipboard(node))
        
        if self.clipboard_node and self.clipboard_node['id'] != node['id']:
             menu.add_command(label="Dán (Paste) vào đây", command=lambda: self.paste_node())
        
        menu.add_separator()
        menu.add_command(label="Xóa", command=lambda: self.delete_node(node))
        
        menu.add_separator()
        
        if node['type'] == 'file':
            menu.add_command(label="Tải xuống", command=lambda: self.download_file(node))
        
        menu.add_command(label="Chia sẻ", command=lambda: self.share_node(node))
        
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def not_implemented(self, feature):
        messagebox.showinfo("Thông báo", f"Tính năng '{feature}' chưa được cập nhật trong Backend C.")

    def clear_ui(self):
        for w in self.winfo_children(): w.destroy()

if __name__ == "__main__":
    app = DriveGUI()
    app.mainloop()