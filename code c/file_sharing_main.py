import tkinter as tk
from tkinter import ttk, messagebox, filedialog, simpledialog
import ctypes
import os
import hashlib

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

cli.cli_list_files.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
cli.cli_list_files.restype = ctypes.POINTER(FileInfo) # Trả về mảng struct

cli.cli_upload.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_longlong, ctypes.c_int, ctypes.c_char_p]
cli.cli_upload.restype = ctypes.c_int

# Tạo folder: name, parent_id, out_msg
cli.cli_create_folder.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p]
cli.cli_create_folder.restype = ctypes.c_int

cli.cli_free_files.argtypes = [ctypes.POINTER(FileInfo)]

# --- GUI ---
class DriveGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("C-Logic Drive Client")
        self.geometry("1000x700")
        
        # Kết nối ngay khi mở app
        if not cli.cli_connect(b"127.0.0.1", 65432):
            messagebox.showerror("Error", "Không thể kết nối Server C")
            self.destroy()
            return

        self.user_id = -1
        self.current_parent_id = 0
        self.path_stack = [] # Breadcrumb: [(id, name), ...]
        self.setup_login()

    # --- UI: LOGIN ---
    def setup_login(self):
        self.clear_ui()
        frame = tk.Frame(self, bg="#f0f2f5")
        frame.pack(fill=tk.BOTH, expand=True)
        
        card = tk.Frame(frame, bg="white", padx=40, pady=40, relief=tk.RAISED, borderwidth=1)
        card.place(relx=0.5, rely=0.5, anchor=tk.CENTER)
        
        tk.Label(card, text="Đăng Nhập (C Core)", font=("Segoe UI", 20, "bold"), bg="white", fg="#1a73e8").pack(pady=(0, 20))
        
        tk.Label(card, text="Username", bg="white", anchor="w").pack(fill=tk.X)
        user_entry = tk.Entry(card, font=("Segoe UI", 12))
        user_entry.pack(fill=tk.X, pady=(0, 10))
        
        tk.Label(card, text="Password", bg="white", anchor="w").pack(fill=tk.X)
        pass_entry = tk.Entry(card, font=("Segoe UI", 12), show="*")
        pass_entry.pack(fill=tk.X, pady=(0, 20))
        
        def login():
            u = user_entry.get().encode('utf-8')
            p = hashlib.sha256(pass_entry.get().encode()).hexdigest().encode('utf-8')
            msg_buf = ctypes.create_string_buffer(256)
            
            uid = cli.cli_login(u, p, msg_buf)
            if uid != -1:
                self.user_id = uid
                self.setup_drive_ui()
            else:
                messagebox.showerror("Login Fail", msg_buf.value.decode('utf-8'))

        tk.Button(card, text="Đăng Nhập", font=("Segoe UI", 12), bg="#1a73e8", fg="white", command=login).pack(fill=tk.X, pady=10)
        
        # Nút chuyển sang Đăng Ký
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
            # Hash password trước khi gửi
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
        tk.Label(header, text="☁ Cloud Drive (C-Backend)", font=("Segoe UI", 14, "bold"), bg="white", fg="#5f6368").pack(side=tk.LEFT, padx=20)
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
        tk.Button(sidebar, text="+ Mới", font=("Segoe UI", 12, "bold"), bg="white", fg="#1a73e8", 
                 relief=tk.FLAT, command=self.show_create_menu).pack(pady=10, padx=20, anchor="w")

        # Right Content
        self.main_content = tk.Frame(body, bg="white")
        body.add(self.main_content)
        
        # Breadcrumb Bar
        self.crumb_frame = tk.Frame(self.main_content, bg="white", height=40)
        self.crumb_frame.pack(fill=tk.X, padx=10, pady=5)
        
        # Grid View
        self.canvas = tk.Canvas(self.main_content, bg="white")
        scrollbar = ttk.Scrollbar(self.main_content, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = tk.Frame(self.canvas, bg="white")

        self.scrollable_frame.bind("<Configure>", lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")))
        self.scrollable_frame.bind("<Button-1>", lambda e: self.on_bg_click(e))
        self.canvas.bind("<Button-1>", lambda e: self.on_bg_click(e))

        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)

        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.update_breadcrumbs()
        self.refresh_nodes()

    def create_sidebar_btn(self, parent, text, cmd):
        btn = tk.Button(parent, text=text, font=("Segoe UI", 11), bg="white", bd=0, anchor="w", padx=20, command=cmd)
        return btn
    
    def on_bg_click(self, event):
        self.canvas.focus_set()

    def show_create_menu(self):
        menu = tk.Menu(self, tearoff=0)
        menu.add_command(label="Tạo Thư Mục", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file)
        
        x = self.winfo_rootx() + 50
        y = self.winfo_rooty() + 150
        
        try:
            menu.tk_popup(x, y)
        finally:
            menu.grab_release()

    # --- ACTIONS ---
    def create_folder(self):
        name = simpledialog.askstring("Mới", "Tên thư mục:")
        if name:
            fname = name.encode('utf-8')
            msg_buf = ctypes.create_string_buffer(256)
            
            # GỌI LOGIC C: CREATE FOLDER
            # Hàm C giả định: int cli_create_folder(char* name, int parent_id, char* out_msg)
            res = cli.cli_create_folder(fname, self.current_parent_id, msg_buf)
            
            msg_str = msg_buf.value.decode('utf-8', errors='ignore')
            if res:
                self.refresh_nodes()
            else:
                messagebox.showerror("Lỗi", msg_str)

    def upload_file(self):
        path = filedialog.askopenfilename()
        if not path: return
        fname = os.path.basename(path).encode('utf-8')
        fpath = path.encode('utf-8')
        fsize = os.path.getsize(path)
        
        msg_buf = ctypes.create_string_buffer(256)
        
        # GỌI LOGIC C: UPLOAD
        res = cli.cli_upload(fpath, fname, fsize, self.current_parent_id, msg_buf)
        
        msg_str = msg_buf.value.decode('utf-8', errors='ignore')
        if res:
            messagebox.showinfo("Upload", f"Thành công: {msg_str}")
            self.refresh_nodes()
        else:
            messagebox.showerror("Upload Lỗi", msg_str)

    # --- NAVIGATION LOGIC ---
    def navigate_to_root(self):
        self.current_parent_id = 0
        self.path_stack = []
        self.update_breadcrumbs()
        self.refresh_nodes()

    def enter_folder(self, node_id, name):
        self.path_stack.append((node_id, name))
        self.current_parent_id = node_id
        self.update_breadcrumbs()
        self.refresh_nodes()
        
    def go_back_to(self, index):
        if index == -1: # Root
            self.navigate_to_root()
        else:
            self.path_stack = self.path_stack[:index+1]
            self.current_parent_id = self.path_stack[-1][0]
            self.update_breadcrumbs()
            self.refresh_nodes()

    def update_breadcrumbs(self):
        for w in self.crumb_frame.winfo_children(): w.destroy()
        
        btn = tk.Button(self.crumb_frame, text="My Drive", font=("Segoe UI", 12, "bold"), bd=0, bg="white", 
                       fg="#5f6368", command=lambda: self.go_back_to(-1))
        btn.pack(side=tk.LEFT)
        
        for idx, (nid, name) in enumerate(self.path_stack):
            tk.Label(self.crumb_frame, text=" > ", bg="white").pack(side=tk.LEFT)
            btn = tk.Button(self.crumb_frame, text=name, font=("Segoe UI", 12), bd=0, bg="white", 
                           fg="#5f6368", command=lambda i=idx: self.go_back_to(i))
            btn.pack(side=tk.LEFT)

    # --- DATA FETCHING (C LOGIC) ---
    def refresh_nodes(self):
        # Clear UI Grid
        for w in self.scrollable_frame.winfo_children(): w.destroy()
        
        count = ctypes.c_int(0)
        # GỌI LOGIC C: LIST FILES
        files_ptr = cli.cli_list_files(self.current_parent_id, ctypes.byref(count))
        
        if count.value > 0 and files_ptr:
            cols = 5
            for i in range(count.value):
                f = files_ptr[i]
                
                # Convert C Data -> Python Data
                node_data = {
                    'id': f.id,
                    'name': f.name.decode('utf-8', errors='ignore'),
                    'type': f.type.decode('utf-8', errors='ignore'),
                    'size': f.size,
                    'owner': f.owner.decode('utf-8', errors='ignore'),
                    'is_shared': f.is_shared
                }
                
                # Vẽ Item lên Grid
                r, c = divmod(i, cols)
                self.draw_node_item(node_data, r, c)
            
            cli.cli_free_files(files_ptr) # Giải phóng bộ nhớ C

    def draw_node_item(self, node, row, col):
        is_folder = (node['type'] == 'folder')
        icon_char = "📁" if is_folder else "📄"
        color = "#FFC107" if is_folder else "#2196F3"
        
        # Container
        frame = tk.Frame(self.scrollable_frame, bg="white", width=120, height=120)
        frame.grid(row=row, column=col, padx=10, pady=10)
        frame.pack_propagate(False)
        
        # Bind events
        frame.bind("<Button-1>", lambda e: self.on_node_click(node))
        frame.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        # Icon
        lbl_icon = tk.Label(frame, text=icon_char, font=("Arial", 30), fg=color, bg="white")
        lbl_icon.pack(expand=True)
        lbl_icon.bind("<Button-1>", lambda e: self.on_node_click(node))
        lbl_icon.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        # Name
        lbl_name = tk.Label(frame, text=node['name'], font=("Segoe UI", 10), bg="white", wraplength=100)
        lbl_name.pack(side=tk.BOTTOM, pady=5)
        
        if node.get('is_shared'):
            tk.Label(frame, text="👥", font=("Arial", 8), bg="white", fg="gray").place(x=5, y=5)

    def on_node_click(self, node):
        if node['type'] == 'folder':
            self.enter_folder(node['id'], node['name'])
        else:
            messagebox.showinfo("File Info", f"File: {node['name']}\nSize: {node['size']} bytes\nOwner: {node['owner']}")

    def show_context_menu(self, event, node):
        menu = tk.Menu(self, tearoff=0)
        if node['type'] == 'file':
            menu.add_command(label="Download", command=lambda: self.not_implemented("Download"))
        menu.add_command(label="Chia sẻ", command=lambda: self.not_implemented("Chia sẻ"))
        
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