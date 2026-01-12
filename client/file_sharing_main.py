import tkinter as tk
from tkinter import ttk, filedialog, simpledialog
import ctypes
import os
import hashlib
from zipfile import ZipFile  
import shutil # Để xóa thư mục tạm thời
import tempfile
import threading

# Thư viện PIL/Pillow cần thiết cho việc tải file ảnh
from PIL import Image, ImageTk

# --- ĐỊNH NGHĨA STRUCT C TRONG PYTHON ---
class PathNode(ctypes.Structure): 
    _pack_ = 1
    _fields_ = [
        ("id", ctypes.c_int),
        ("name", ctypes.c_char * 256),
    ]

class FileInfo(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("id", ctypes.c_int),
        ("name", ctypes.c_char * 256),
        ("type", ctypes.c_char * 10),
        ("size", ctypes.c_longlong),
        ("owner", ctypes.c_char * 50),
        ("is_shared", ctypes.c_int)
    ]
    
class ShareEntry(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("username", ctypes.c_char * 50),
        ("permission", ctypes.c_char * 50),
    ]

# --- LOAD THƯ VIỆN C ---
lib_name = "./client_logic.so" if os.name != 'nt' else "./client_logic.dll"
try:
    cli = ctypes.CDLL(lib_name)
except OSError:
    # Thoát im lặng, không in ra terminal
    os._exit(1)

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

# Lấy đường dẫn
cli.cli_get_node_path.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
cli.cli_get_node_path.restype = ctypes.POINTER(PathNode)

cli.cli_free_path.argtypes = [ctypes.POINTER(PathNode)]

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

# Share: node_id, target_username, permission, out_msg 
cli.cli_share_node.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_share_node.restype = ctypes.c_int

# Khai báo hàm lấy danh sách share
cli.cli_get_share_list.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
cli.cli_get_share_list.restype = ctypes.POINTER(ShareEntry)

# Khai báo hàm giải phóng bộ nhớ
cli.cli_free_share_list.argtypes = [ctypes.POINTER(ShareEntry)]
cli.cli_free_share_list.restype = None

# Khai báo hàm xóa share
cli.cli_remove_share.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p]
cli.cli_remove_share.restype = ctypes.c_int

cli.cli_free_files.argtypes = [ctypes.POINTER(FileInfo)]

# --- GUI ---
class DriveGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("File Sharing App")
        self.geometry("1000x700")
        
        # [MỚI] Tạo khóa để bảo vệ Socket tránh xung đột
        self.cli_lock = threading.Lock()
        
        # --- CLIPBOARD STATE ---
        self.clipboard_node = None
        self.clipboard_action = None
        self.is_searching = False 
        self.logged_in_username = None 
        
        # --- TẢI VÀ CHUẨN BỊ ICONS ---
        self.load_icons()
        
        # Kết nối ngay khi mở app
        if not cli.cli_connect(b"127.0.0.1", 65432):
            self.show_custom_message("Error", "Không thể kết nối Server C", is_error=True)
            self.destroy()
            return

        self.user_id = -1
        self.current_parent_id = 0
        self.path_stack = []
        self.last_node_data = []
        self.setup_login()
        
    def load_icons(self):
        icon_path = "icons"
        self.icon_folder = None
        self.icon_file = None
        self.icon_shared = None
        self.use_image_icons = False

        
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
            pass
            self.use_image_icons = True
        except Exception as e:
            pass

    # --- HÀM THÔNG BÁO TÙY CHỈNH ---
    def show_custom_message(self, title, message, is_error=False, parent=None):
        if parent is None: parent = self
        dialog = tk.Toplevel(parent)
        dialog.title(title)
        dialog.geometry("450x150") 
        dialog.resizable(False, False)
        
        dialog.transient(parent)
        
        dialog.wait_visibility() # Chờ cửa sổ hiện lên hẳn rồi mới chiếm quyền
        dialog.grab_set()
        
        content_frame = tk.Frame(dialog, padx=20, pady=20)
        content_frame.pack(fill=tk.BOTH, expand=True)
        
        lbl = tk.Label(content_frame, text=message, font=("Segoe UI", 11), 
                       wraplength=400, justify="center")
        lbl.pack(expand=True)
        
        btn_frame = tk.Frame(dialog, pady=10) 
        btn_frame.pack(side=tk.BOTTOM, fill=tk.X)
        
        tk.Button(btn_frame, text="Đóng", command=dialog.destroy, 
                  font=("Segoe UI", 10), width=12).pack()
        
        self.wait_window(dialog)

    # --- HÀM XÁC NHẬN TÙY CHỈNH ---
    def show_custom_confirmation(self, title, message, parent=None):
        if parent is None: parent = self
        dialog = tk.Toplevel(parent)
        dialog.title(title)
        dialog.geometry("450x160")
        dialog.resizable(False, False)
        dialog.transient(parent)
        
        dialog.wait_visibility() # Chờ cửa sổ hiện lên hẳn
        dialog.grab_set()

        content_frame = tk.Frame(dialog, padx=20, pady=20)
        content_frame.pack(fill=tk.BOTH, expand=True)

        tk.Label(content_frame, text=message, font=("Segoe UI", 11), 
                 wraplength=400, justify="center").pack(expand=True)

        self.confirm_result = False

        def on_yes():
            self.confirm_result = True
            dialog.destroy()

        def on_no():
            self.confirm_result = False
            dialog.destroy()

        btn_frame = tk.Frame(dialog, pady=15)
        btn_frame.pack(side=tk.BOTTOM, fill=tk.X)

        tk.Button(btn_frame, text="Có", command=on_yes, 
                  font=("Segoe UI", 10, "bold"), bg="#1a73e8", fg="white", 
                  width=12).pack(side=tk.LEFT, padx=(80, 10), expand=True)
        
        tk.Button(btn_frame, text="Không", command=on_no, 
                  font=("Segoe UI", 10), width=12).pack(side=tk.LEFT, padx=(10, 80), expand=True)

        self.wait_window(dialog)
        return self.confirm_result
    
    def start_auto_refresh(self):
        if hasattr(self, 'user_id') and self.user_id != -1 and not self.is_searching:
            try:
                # Thêm wait_lock=False để nếu đang bận upload thì bỏ qua
                self.refresh_nodes(wait_lock=False)
            except Exception as e:
                pass
        
        self.auto_refresh_job = self.after(2000, self.start_auto_refresh)
        
    # Sửa lại định nghĩa hàm có thêm tham số wait_lock
    def refresh_nodes(self, search_mode=False, wait_lock=True):
        # 0. Kiểm tra khóa
        if not wait_lock:
            # Nếu là Auto Refresh (wait_lock=False), kiểm tra xem có ai đang dùng Socket không
            # Nếu đang khóa (blocking=False trả về False) -> BỎ QUA luợt refresh này
            if not self.cli_lock.acquire(blocking=False):
                pass
                return 
        else:
            # Nếu là lệnh bắt buộc (khi click nút), thì phải chờ bằng được
            self.cli_lock.acquire()

        try:
            # 1. Lấy dữ liệu từ Server
            count = ctypes.c_int(0)
            files_ptr = None
            
            if search_mode:
                # Lưu ý: search cũng cần release lock nếu return sớm, 
                # nhưng ở đây ta để finally xử lý
                pass 
            else:
                files_ptr = cli.cli_list_files(self.current_parent_id, ctypes.byref(count))

            # 2. Chuyển dữ liệu C sang List Python
            new_data = []
            if count.value > 0 and files_ptr:
                for i in range(count.value):
                    f = files_ptr[i]
                    node = {
                        'id': f.id,
                        'name': f.name.decode('utf-8', errors='ignore').strip('\x00').strip(),
                        'type': f.type.decode('utf-8', errors='ignore').strip('\x00').strip(),
                        'size': f.size,
                        'owner': f.owner.decode('utf-8', errors='ignore').strip('\x00').strip(),
                        'is_shared': f.is_shared
                    }
                    new_data.append(node)
                cli.cli_free_files(files_ptr)
                
                # Sắp xếp
                new_data.sort(key=lambda x: x['id'])

            # --- LOGIC CHỐNG NHÁY ---
            if not hasattr(self, 'last_node_data') or self.last_node_data is None:
                self.last_node_data = []

            if self.last_node_data == new_data:
                return # Giống hệt -> Không vẽ lại

            self.last_node_data = new_data
            
            # 3. Vẽ lại giao diện
            for w in self.scrollable_frame.winfo_children(): w.destroy()

            if not new_data:
                return

            cols = 5
            for i, node in enumerate(new_data):
                r, c = divmod(i, cols)
                self.draw_node_item(node, r, c)
            
        finally:
            # Bắt buộc phải nhả khóa dù có lỗi hay không
            self.cli_lock.release()
    
    # --- UI: LOGIN ---
    def setup_login(self):
        # Hủy auto refresh nếu đang chạy
        if hasattr(self, 'auto_refresh_job') and self.auto_refresh_job:
            self.after_cancel(self.auto_refresh_job)
            self.auto_refresh_job = None
            
        self.clear_ui()
        # Khi vào màn hình đăng nhập, xóa username đã đăng nhập trước đó
        self.logged_in_username = None
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

        self.login_user_entry = user_entry
        
        def login():
            u_raw = user_entry.get()
            u = u_raw.encode('utf-8')
            p = hashlib.sha256(pass_entry.get().encode()).hexdigest().encode('utf-8')
            msg_buf = ctypes.create_string_buffer(256)
            
            uid = cli.cli_login(u, p, msg_buf)
            if uid != -1:
                self.user_id = uid
                self.logged_in_username = u_raw
                
                # RESET TRẠNG THÁI 
                self.current_parent_id = 0  # Luôn bắt đầu từ Root
                self.path_stack = []        # Xóa lịch sử đường dẫn cũ
                self.is_searching = False   # Tắt chế độ tìm kiếm (nếu có)
                
                self.setup_drive_ui()
            else:
                self.show_custom_message("Login Fail", msg_buf.value.decode('utf-8'), is_error=True)

        tk.Button(card, text="Đăng Nhập", font=("Segoe UI", 12), bg="#1a73e8", fg="white", command=login).pack(fill=tk.X, pady=10)
        
        tk.Button(card, text="Chưa có tài khoản? Đăng ký ngay", font=("Segoe UI", 10), bg="white", fg="#1a73e8", 
                 bd=0, cursor="hand2", command=self.setup_register).pack(fill=tk.X, pady=10)

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
                self.show_custom_message("Lỗi", "Vui lòng điền đầy đủ thông tin", is_error=True)
                return
                
            msg_buf = ctypes.create_string_buffer(256)
            res = cli.cli_register(u, p, fn, em, msg_buf)
            
            msg_str = msg_buf.value.decode('utf-8', errors='ignore')
            if res:
                self.show_custom_message("Thành công", "Đăng ký thành công! Vui lòng đăng nhập.")
                self.setup_login()
            else:
                self.show_custom_message("Thất bại", msg_str, is_error=True)

        tk.Button(card, text="Đăng Ký", font=("Segoe UI", 12), bg="#4CAF50", fg="white", command=submit_reg).pack(fill=tk.X, pady=10)
        tk.Button(card, text="Quay lại Đăng nhập", font=("Segoe UI", 10), bg="white", fg="#555", 
                 bd=0, cursor="hand2", command=self.setup_login).pack(fill=tk.X)

    # --- UI: MAIN DRIVE ---
    def setup_drive_ui(self):
        self.clear_ui()
        
        # Header Bar
        header = tk.Frame(self, bg="white", height=50, bd=1, relief=tk.RAISED)
        header.pack(side=tk.TOP, fill=tk.X)
        tk.Label(header, text="File Sharing App", font=("Segoe UI", 14, "bold"), bg="white", fg="#5f6368").pack(side=tk.LEFT, padx=20)
        
        # Thanh tìm kiếm
        search_frame = tk.Frame(header, bg="white") 
        self.search_entry = tk.Entry(search_frame, font=("Segoe UI", 13), width=32) 
        self.search_entry.pack(side=tk.LEFT, padx=5, pady=10) 
        tk.Button(search_frame, text="Tìm kiếm", command=self.search_action).pack(side=tk.LEFT, padx=4) 
        search_frame.pack(side=tk.LEFT, padx=22)
        
        tk.Button(header, text="Đăng xuất", bg="#ff4d4f", fg="white", command=self.setup_login).pack(side=tk.RIGHT, padx=10, pady=10)

        # Body Split
        body = tk.PanedWindow(self, orient=tk.HORIZONTAL, bg="#f0f0f0")
        body.pack(fill=tk.BOTH, expand=True)
        
        # Left Sidebar
        sidebar = tk.Frame(body, bg="white", width=200)
        body.add(sidebar, minsize=150)
        
        self.create_sidebar_btn(sidebar, "My Drive", lambda: self.navigate_to_root()).pack(fill=tk.X, pady=2)
        tk.Frame(sidebar, height=1, bg="#ddd").pack(fill=tk.X, pady=10)
        
        # Nút "+ Mới" mở menu
        tk.Button(sidebar, text="Tạo Mới", font=("Segoe UI", 12, "bold"), bg="#1a73e8", fg="white", 
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
        
        self.scrollable_frame.bind("<Button-1>", lambda e: (self.main_content.focus_set(), self.scrollable_frame.focus_set()))
        self.canvas.bind("<Button-1>", lambda e: (self.main_content.focus_set(), self.canvas.focus_set()))

        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)

        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        self.update_breadcrumbs()
        self.refresh_nodes()
        self.update_sidebar_paste_state()
        
        self.last_node_data = []

        # Status bar (username bottom-left)
        status_bar = tk.Frame(self, bg="#f5f5f5", height=26)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        self.status_username_lbl = tk.Label(status_bar, text=f"Người dùng: {self.get_current_username()}",
                           font=("Segoe UI", 10), bg="#f5f5f5", fg="#333")
        self.status_username_lbl.pack(side=tk.LEFT, padx=8)

        # Bắt đầu chạy auto refresh
        self.refresh_nodes()
        self.start_auto_refresh()

        # Cập nhật hiển thị username (nếu cần)
        self.update_status_username()

    def create_sidebar_btn(self, parent, text, cmd):
        btn = tk.Button(parent, text=text, font=("Segoe UI", 11), bg="white", bd=0, anchor="w", padx=20, command=cmd)
        return btn
    
    def update_sidebar_paste_state(self):
        pass

    def show_canvas_context_menu(self, event):
        menu = tk.Menu(self, tearoff=0)
        
        menu.add_command(label="Tạo Thư Mục Mới", command=self.create_folder)
        menu.add_command(label="Tải File Lên", command=self.upload_file_dialog)
        menu.add_command(label="Tải Thư Mục Lên", command=self.upload_folder_dialog)
        
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
        menu.add_command(label="Tải File Lên", command=self.upload_file_dialog)
        menu.add_command(label="Tải Thư Mục Lên", command=self.upload_folder_dialog)
        
        if self.clipboard_node:
            menu.add_separator()
            menu.add_command(label="Dán (Paste)", command=self.paste_node)
        
        x = self.winfo_rootx() + 50
        y = self.winfo_rooty() + 150
        
        try:
            menu.tk_popup(x, y)
        finally:
            menu.grab_release()

    # --- UPLOAD/DOWNLOAD LOGIC ---
    def zip_folder(self, folder_path, zip_name):
        base_dir = os.path.basename(folder_path)
        try:
            with ZipFile(zip_name, 'w') as zipf:
                for root, _, files in os.walk(folder_path):
                    for file in files:
                        file_path = os.path.join(root, file)
                        arcname = os.path.relpath(file_path, os.path.dirname(folder_path))
                        zipf.write(file_path, arcname)
            return zip_name
        except Exception as e:
            self.show_custom_message("Lỗi Nén", f"Không thể nén thư mục: {e}", is_error=True)
            return None

    def upload_file_dialog(self):
        path = filedialog.askopenfilename()
        if not path: return
        fname = os.path.basename(path)
        fpath = path
        fsize = os.path.getsize(path)
        
        self.execute_cli_action(cli.cli_upload, fpath, fname, fsize, self.current_parent_id, 
                                success_msg="Tải lên File thành công")

    def upload_folder_dialog(self):
        local_root = filedialog.askdirectory()
        if not local_root: return
        
        base_name = os.path.basename(local_root)
        
        def run_thread():
            try:
                self.after(0, lambda: self.title(f"Đang tạo cấu trúc: {base_name}... (Vui lòng đợi)"))
                
                msg = ctypes.create_string_buffer(256)

                # Đếm tổng số file để log gọn (server sẽ ghi log 1 lần cho folder)
                total_files_count = 0
                for _root, _dirs, _files in os.walk(local_root):
                    total_files_count += len(_files)

                # Thêm đuôi __UPLOAD_REQ__{count} để Server biết đây là upload folder và số file
                # Server sẽ tự động cắt đuôi này đi trước khi tạo, dùng count để log
                root_name_req = (f"{base_name}__UPLOAD_REQ__{total_files_count}").encode('utf-8')

                with self.cli_lock:
                    root_id = cli.cli_create_folder(root_name_req, self.current_parent_id, msg)
                
                if root_id == -1:
                    raise Exception(f"Lỗi tạo root: {msg.value.decode('utf-8')}")
                
                folder_map = {local_root: root_id}

                for root, dirs, files in os.walk(local_root):
                    current_server_id = folder_map[root]
                    
                    # 1. Tạo các folder con (Cũng thêm đuôi mật mã)
                    for d in dirs:
                        local_dir_path = os.path.join(root, d)
                        sub_name_req = (d + "__UPLOAD_REQ__").encode('utf-8')
                        
                        with self.cli_lock:
                            new_sub_id = cli.cli_create_folder(sub_name_req, current_server_id, msg)
                            
                        if new_sub_id != -1:
                            folder_map[local_dir_path] = new_sub_id
                        else:
                            pass

                    # 2. Upload các file (thêm __NO_LOG__ để server không ghi log từng file)
                    for f in files:
                        local_file_path = os.path.join(root, f)
                        fsize = os.path.getsize(local_file_path)

                        silent_name = (f + "__NO_LOG__").encode('utf-8')
                        with self.cli_lock:
                            cli.cli_upload(local_file_path.encode('utf-8'), silent_name, fsize, current_server_id, msg)

                self.after(0, lambda: self.show_custom_message("Thành công", f"Đã upload xong {total_files_count} file trong folder '{base_name}'!"))
                self.after(0, lambda: self.refresh_nodes(wait_lock=True))

            except Exception as e:
                pass
                self.after(0, lambda: self.show_custom_message("Lỗi Upload", str(e), is_error=True))
            finally:
                self.after(0, lambda: self.title("File Sharing App"))

        threading.Thread(target=run_thread, daemon=True).start()
        
    def download_file(self, node):
        node_id = node['id']
        name = node['name']
        node_type = node['type']

        save_name = name if node_type == "file" else f"{name}.zip"
        
        save_path = filedialog.asksaveasfilename(initialfile=save_name)
        if not save_path: return

        def run_thread():
            msg = ctypes.create_string_buffer(256)
            self.after(0, lambda: self.title(f"Đang tải {name}..."))
            with self.cli_lock:
                res = cli.cli_download(int(node_id), save_path.encode('utf-8'), msg)
            
            if res == 1:
                if node_type == "folder":
                    message = f"Download thành công: {save_name}\nFile zip đã được lưu tại:\n{save_path}"
                else:
                    message = f"Download thành công: {name}"
                
                self.after(0, lambda: self.show_custom_message("Xong", message))
            else:
                err = msg.value.decode('utf-8')
                self.after(0, lambda: self.show_custom_message("Lỗi", err, is_error=True))
            
            self.after(0, lambda: self.title("File Sharing App"))

        threading.Thread(target=run_thread, daemon=True).start()
            
    # --- ACTIONS ---
    def execute_cli_action(self, cli_func, *args, success_msg="Thành công"):
        msg_buf = ctypes.create_string_buffer(256)
        c_args = []
        for arg in args:
            if isinstance(arg, str):
                c_args.append(arg.encode('utf-8'))
            else:
                c_args.append(arg)
        
        # Bọc trong lock
        with self.cli_lock:
            res = cli_func(*c_args, msg_buf)
            
        msg_str = msg_buf.value.decode('utf-8', errors='ignore')
        
        if res:
            self.show_custom_message(success_msg, msg_str)
            self.refresh_nodes(wait_lock=True) # Refresh ngay lập tức thì cần chờ
            return True
        else:
            self.show_custom_message("Thất bại", msg_str, is_error=True)
            return False

    def create_folder(self):
        name = simpledialog.askstring("Tạo Thư Mục", "Tên thư mục:")
        if name:
            self.execute_cli_action(cli.cli_create_folder, name, self.current_parent_id, 
                                    success_msg="Tạo thư mục thành công")

    def search_action(self):
        query = self.search_entry.get().strip()
        if not query:
            if self.is_searching:
                self.navigate_to_root()
            else:
                self.show_custom_message("Thông báo", "Vui lòng nhập từ khóa để tìm kiếm.")
            return

        count = ctypes.c_int(0)
        search_query_bytes = query.encode('utf-8')
        files_ptr = cli.cli_search_files(search_query_bytes, ctypes.byref(count))

        if count.value > 0 and files_ptr:
            results = []
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
                results.append(node_data)
            cli.cli_free_files(files_ptr)

            self.show_search_selection_dialog(results)
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
            self.refresh_nodes()
        else:
            self.show_custom_message("Tìm kiếm", f"Không tìm thấy kết quả nào cho '{query}'.")
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
            self.refresh_nodes()
            
    def show_search_selection_dialog(self, results):
        dialog = tk.Toplevel(self)
        dialog.title(f"Kết quả tìm kiếm ({len(results)} mục)")
        dialog.geometry("600x400")
        dialog.transient(self)
        dialog.grab_set()

        tk.Label(dialog, text="Vui lòng chọn mục bạn muốn điều hướng đến:", 
                 font=("Segoe UI", 12, "bold")).pack(pady=10, padx=10, anchor='w')

        list_frame = ttk.Frame(dialog)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

        canvas = tk.Canvas(list_frame)
        vscrollbar = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=canvas.yview)
        canvas.configure(yscrollcommand=vscrollbar.set)

        scrollable_content = tk.Frame(canvas)
        scrollable_content.bind("<Configure>", lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=scrollable_content, anchor="nw")

        vscrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        canvas.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        for result_node in results:
            path_count = ctypes.c_int(0)
            path_ptr = cli.cli_get_node_path(result_node['id'], ctypes.byref(path_count))
            
            if path_count.value > 0 and path_ptr:
                path_nodes = [path_ptr[i].name.decode('utf-8', errors='ignore') for i in range(path_count.value)]
                cli.cli_free_path(path_ptr)
                path_display = "My Drive > " + " > ".join(path_nodes)
            else:
                path_display = "Không thể lấy đường dẫn."

            item_frame = tk.Frame(scrollable_content, bd=1, relief=tk.RAISED, bg="#f9f9f9", padx=5, pady=5)
            item_frame.pack(fill=tk.X, pady=2)
            
            lbl_path = tk.Label(item_frame, text=path_display, anchor='w', font=("Segoe UI", 10), bg="#f9f9f9", fg="#1a73e8")
            lbl_path.pack(fill=tk.X)
            
            btn_navigate = tk.Button(item_frame, text="Điều hướng & Thao tác", 
                                     command=lambda nid=result_node['id'], name=result_node['name'], path_len=path_count.value: self.perform_navigate_and_highlight(dialog, nid, name, path_len))
            btn_navigate.pack(pady=5)
        
        tk.Button(dialog, text="Đóng", command=dialog.destroy).pack(pady=10)
        dialog.wait_window(dialog)

    def perform_navigate_and_highlight(self, dialog, node_id, node_name, path_len):
        parent_id = 0
        path_count = ctypes.c_int(0)
        path_ptr = cli.cli_get_node_path(node_id, ctypes.byref(path_count))

        if path_count.value > 1 and path_ptr:
            parent_node = path_ptr[path_count.value - 2]
            parent_id = parent_node.id
            cli.cli_free_path(path_ptr)
        elif path_count.value == 1 and path_ptr:
             parent_id = 0
             cli.cli_free_path(path_ptr)

        if parent_id >= 0:
            self.current_parent_id = parent_id
            self.path_stack = []
            
            if path_count.value > 1:
                path_ptr_full = cli.cli_get_node_path(node_id, ctypes.byref(path_count))
                for i in range(path_count.value - 1):
                     n = path_ptr_full[i]
                     self.path_stack.append((n.id, n.name.decode('utf-8', errors='ignore')))
                cli.cli_free_path(path_ptr_full)

            self.update_breadcrumbs()
            self.refresh_nodes()
            dialog.destroy()
            self.show_custom_message("Điều hướng thành công", f"Đã chuyển đến thư mục chứa '{node_name}'.")

    def share_node(self, node):
        target = simpledialog.askstring("Chia sẻ", f"Nhập tên người muốn chia sẻ '{node['name']}':")
        if not target: return
        
        if target == self.logged_in_username:
            self.show_custom_message("Lỗi", "Bạn là chủ sở hữu, không cần tự chia sẻ!", is_error=True)
            return

        perm_str = self.ask_permission_dialog("Chia sẻ tài liệu", target, current_perm="read")
        
        if perm_str:
            self.execute_cli_action(cli.cli_share_node, node['id'], target, perm_str, 
                                    success_msg=f"Đã chia sẻ cho {target} quyền: {perm_str}")
        
    def get_current_username(self):
        return self.logged_in_username if self.logged_in_username else "Unknown" 

    def update_status_username(self):
        if hasattr(self, 'status_username_lbl'):
            self.status_username_lbl.config(text=f"Người dùng: {self.get_current_username()}")

    def rename_node(self, node):
        new_name = simpledialog.askstring("Đổi tên", f"Đổi tên '{node['name']}' thành:", initialvalue=node['name'])
        if new_name and new_name != node['name']:
            self.execute_cli_action(cli.cli_rename, node['id'], new_name, 
                                    success_msg="Đổi tên thành công")

    def delete_node(self, node):
        if self.show_custom_confirmation("Xác nhận Xóa", f"Bạn có chắc chắn muốn xóa '{node['name']}' không? \nHành động này không thể hoàn tác."):
            self.execute_cli_action(cli.cli_delete, node['id'], 
                                    success_msg="Xóa thành công")

    def copy_node_to_clipboard(self, node):
        self.clipboard_node = node
        self.clipboard_action = 'copy'
        self.show_custom_message("Clipboard", f"Đã Copy '{node['name']}'. Chọn Paste để dán.")
        self.update_sidebar_paste_state()

    def cut_node_to_clipboard(self, node):
        self.clipboard_node = node
        self.clipboard_action = 'move'
        self.show_custom_message("Clipboard", f"Đã Cut '{node['name']}'. Chọn Paste để di chuyển.")
        self.update_sidebar_paste_state()
        
    def paste_node(self):
        if not self.clipboard_node:
            self.show_custom_message("Lỗi", "Clipboard trống.", is_error=True)
            return

        source_node = self.clipboard_node
        target_id = self.current_parent_id
        
        if source_node['id'] == target_id:
             self.show_custom_message("Lỗi", "Không thể dán (Copy/Move) file/folder vào chính nó.", is_error=True)
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
        self.current_parent_id = 0
        self.path_stack = []
        self.is_searching = False
        self.search_entry.delete(0, tk.END)
        for w in self.scrollable_frame.winfo_children(): w.destroy()
        self.last_node_data = None
        self.update_breadcrumbs()
        self.refresh_nodes()

    def enter_folder(self, node_id, name):
        if self.is_searching:
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
        
        self.path_stack.append((node_id, name))
        self.current_parent_id = node_id
        # 1. Xóa ngay giao diện cũ để người dùng biết là đang chuyển trang
        for w in self.scrollable_frame.winfo_children(): w.destroy()
        # 2. Reset bộ nhớ đệm để refresh_nodes không tự ý return
        self.last_node_data = None 

        self.refresh_nodes()
        self.update_breadcrumbs()
        
    def go_back_to(self, index):
        if index == -1: # Root
            self.navigate_to_root()
        else:
            self.path_stack = self.path_stack[:index+1]
            self.current_parent_id = self.path_stack[-1][0]
            self.is_searching = False
            self.search_entry.delete(0, tk.END)
            for w in self.scrollable_frame.winfo_children(): w.destroy()
            self.last_node_data = None
            self.update_breadcrumbs()
            self.refresh_nodes()

    def update_breadcrumbs(self):
        for w in self.crumb_frame.winfo_children(): w.destroy()
        
        if self.is_searching:
            tk.Label(self.crumb_frame, text=f"🔍 Kết quả tìm kiếm cho: '{self.search_query}'", 
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
            
    def draw_node_item(self, node, row, col):
        is_folder = (node['type'] == 'folder')
        
        icon_img = None
        icon_char = ""
        color = ""

        if self.use_image_icons:
            icon_img = self.icon_folder if is_folder else self.icon_file
        else:
            if is_folder:
                icon_char = "🗀"
                color = "#FFC107"
            else:
                icon_char = "🗋"
                color = "#4285F4" 
        
        frame = tk.Frame(self.scrollable_frame, bg="white", width=120, height=120, padx=5, pady=5)
        
        if self.clipboard_node and self.clipboard_action == 'move' and self.clipboard_node['id'] == node['id']:
             frame.config(bg="#f0f0f0", relief=tk.GROOVE)

        frame.grid(row=row, column=col, padx=10, pady=10)
        frame.pack_propagate(False)
        
        frame.bind("<Double-1>", lambda e: self.on_node_double_click(node))
        frame.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
        
        if icon_img:
            lbl_icon = tk.Label(frame, image=icon_img, bg="white")
            lbl_icon.image = icon_img
            lbl_icon.pack(expand=True)
        else:
            lbl_icon = tk.Label(frame, text=icon_char, font=("Arial", 40), fg=color, bg="white")
            lbl_icon.pack(expand=True)
        
        lbl_name = tk.Label(
            frame,
            text=node['name'],
            font=("Segoe UI", 10),
            bg="white",
            wraplength=100
        )
        lbl_name.pack(side=tk.BOTTOM, pady=5)
        
        if node.get('is_shared'):
            if self.icon_shared and self.use_image_icons:
                 lbl_shared = tk.Label(frame, image=self.icon_shared, bg="white")
                 lbl_shared.image = self.icon_shared
                 lbl_shared.place(x=5, y=5)
            else:
                 tk.Label(frame, text="👥", font=("Arial", 8), bg="white", fg="gray").place(x=5, y=5)
            
        for widget in [lbl_icon, lbl_name]:
            widget.bind("<Double-1>", lambda e: self.on_node_double_click(node))
            widget.bind("<Button-3>", lambda e: self.show_context_menu(e, node))
            widget.bind("<Button-1>", lambda e: None) 

    def on_node_double_click(self, node):
        if node['type'] == 'folder':
            self.enter_folder(node['id'], node['name'])
        else:
            self.show_custom_message("File Info", f"File: {node['name']}\nSize: {node['size']} bytes\nOwner: {node['owner']}")

    def show_context_menu(self, event, node):
        menu = tk.Menu(self, tearoff=0)
        
        menu.add_command(label="Thông tin", command=lambda: self.show_properties(node))
        menu.add_command(label="Đổi tên", command=lambda: self.rename_node(node))
        menu.add_command(label="Sao chép (Copy)", command=lambda: self.copy_node_to_clipboard(node))
        menu.add_command(label="Cắt (Cut) / Di chuyển", command=lambda: self.cut_node_to_clipboard(node))
        
        if self.clipboard_node and self.clipboard_node['id'] != node['id']:
             menu.add_command(label="Dán (Paste) vào đây", command=lambda: self.paste_node())
        
        menu.add_separator()
        menu.add_command(label="Xóa", command=lambda: self.delete_node(node))
        
        menu.add_separator()
        menu.add_command(label="Tải xuống", command=lambda: self.download_file(node))
        
        menu.add_separator()
        menu.add_command(label="Chia sẻ", command=lambda: self.share_node(node))
        
        menu.add_command(label="Quản lý quyền", command=lambda: self.manage_permissions(node))
        
        try:
            menu.tk_popup(event.x_root, event.y_root)
        finally:
            menu.grab_release()

    def not_implemented(self, feature="Unknown"):
        self.show_custom_message("Thông báo", f"Tính năng '{feature}' chưa được cập nhật trong Backend C.")

    def clear_ui(self):
        for w in self.winfo_children(): w.destroy()
        
    def show_properties(self, node):
        # Nếu là folder, ẩn thông tin kích thước
        if node.get('type') == 'folder':
            msg = f"""
        Tên: {node['name']}
        Loại: {node['type']}
        Chủ sở hữu: {node['owner']}
        """
        else:
            msg = f"""
        Tên: {node['name']}
        Loại: {node['type']}
        Kích thước: {node['size']} bytes
        Chủ sở hữu: {node['owner']}
        """

        self.show_custom_message(f"Thông tin: {node['name']}", msg)
    
    def manage_permissions(self, node):
        is_owner = (node['owner'] == self.logged_in_username)

        def get_display_text_from_raw(raw_str):
            perms_set = set(raw_str.replace(" ", "").split(","))
            labels = []
            if "read" in perms_set: labels.append("Đọc")
            if "write" in perms_set: labels.append("Ghi")
            if "share" in perms_set: labels.append("Chia sẻ")
            
            if not labels: return "Chỉ xem (Không có quyền thao tác)"
            if len(labels) == 1: return labels[0]
            return ", ".join(labels[:-1]) + " và " + labels[-1]

        if not is_owner:
            dialog = tk.Toplevel(self)
            dialog.title(f"Thông tin quyền hạn")
            dialog.geometry("400x250")
            dialog.resizable(False, False)
            
            count = ctypes.c_int(0)
            ptr = cli.cli_get_share_list(node['id'], ctypes.byref(count))
            
            my_perm_text = "Không xác định"
            
            if count.value > 0 and ptr:
                p_raw = ptr[0].permission.decode('utf-8', errors='ignore')
                my_perm_text = get_display_text_from_raw(p_raw)
                cli.cli_free_share_list(ptr)
            else:
                my_perm_text = "Không có quyền đặc biệt"

            tk.Label(dialog, text="Quyền hạn của bạn", font=("Segoe UI", 14, "bold"), fg="#1a73e8").pack(pady=(30, 10))
            tk.Label(dialog, text=f"Đối với: {node['name']}", font=("Segoe UI", 10, "italic"), fg="gray").pack()
            tk.Label(dialog, text=f"Bạn có quyền:\n{my_perm_text}", font=("Segoe UI", 13), fg="#333", justify="center").pack(pady=20)
            tk.Button(dialog, text="Đóng", command=dialog.destroy, width=10, bg="#f0f0f0").pack(side=tk.BOTTOM, pady=20)
            return

        dialog = tk.Toplevel(self)
        dialog.title(f"Quản lý quyền: {node['name']}")
        dialog.geometry("700x500")
        
        tk.Label(dialog, text=f"Quản lý quyền hạn (Chủ sở hữu)", font=("Segoe UI", 12, "bold"), fg="#1a73e8").pack(pady=10)

        list_frame = tk.Frame(dialog)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=10)
        
        columns = ("user", "perm")
        tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=10)
        tree.heading("user", text="Người dùng")
        tree.heading("perm", text="Quyền hạn")
        tree.column("user", width=200)
        tree.column("perm", width=450)
        tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        def load_data_owner():
            for item in tree.get_children(): tree.delete(item)
            tree.insert("", tk.END, values=(f"{node['owner']} (Chủ sở hữu)", "Toàn quyền (Đọc, Ghi, Xóa, Chia sẻ)"), tags=('owner',))
            count = ctypes.c_int(0)
            ptr = cli.cli_get_share_list(node['id'], ctypes.byref(count))
            
            if count.value > 0 and ptr:
                user_perms = {} 
                for i in range(count.value):
                    u = ptr[i].username.decode('utf-8', errors='ignore')
                    p_raw = ptr[i].permission.decode('utf-8', errors='ignore')
                    
                    if u not in user_perms: user_perms[u] = set()
                    parts = p_raw.replace(" ", "").split(",") 
                    for part in parts:
                        if part: user_perms[u].add(part)
                
                for user, perms in user_perms.items():
                    raw_combined = ",".join(perms)
                    display_text = get_display_text_from_raw(raw_combined)
                    tree.insert("", tk.END, values=(user, display_text), tags=(raw_combined,))
                
                cli.cli_free_share_list(ptr)

        load_data_owner()

        btn_frame = tk.Frame(dialog)
        btn_frame.pack(fill=tk.X, padx=10, pady=10)

        def edit_share():
            sel = tree.selection()
            if not sel:
                self.show_custom_message("Thông báo", "Vui lòng chọn người dùng để sửa quyền.", parent=dialog)
                return
            
            item = tree.item(sel[0])
            username_display = item['values'][0]
            
            if "Chủ sở hữu" in username_display:
                self.show_custom_message("Lỗi", "Không thể sửa quyền của Chủ sở hữu.", is_error=True, parent=dialog)
                return
            
            current_perm = item['tags'][0] if item['tags'] else ""
            new_perm = self.ask_permission_dialog("Sửa quyền truy cập", username_display, current_perm)
            
            if new_perm:
                self.execute_cli_action(cli.cli_share_node, node['id'], username_display, new_perm, success_msg=None)
                load_data_owner()

        def remove_share():
            sel = tree.selection()
            if not sel: return
            username = tree.item(sel[0])['values'][0]
            if "Chủ sở hữu" in username: return

            if self.show_custom_confirmation("Xác nhận", f"Gỡ bỏ quyền truy cập của {username}?", parent=dialog):
                msg = ctypes.create_string_buffer(256)
                res = cli.cli_remove_share(node['id'], username.encode('utf-8'), msg)
                if res: load_data_owner()
                else: self.show_custom_message("Lỗi", msg.value.decode('utf-8'), is_error=True, parent=dialog)

        tk.Button(btn_frame, text="Sửa quyền", command=edit_share, bg="#FF9800", fg="white").pack(side=tk.LEFT, padx=5)
        tk.Button(btn_frame, text="Gỡ quyền", command=remove_share, bg="#ff4d4f", fg="white").pack(side=tk.LEFT, padx=5)
        tk.Button(btn_frame, text="Đóng", command=dialog.destroy).pack(side=tk.RIGHT)
    
    def ask_permission_dialog(self, title, username, current_perm=""):
        dialog = tk.Toplevel(self)
        dialog.title(title)
        dialog.geometry("400x300")
        dialog.transient(self)
        dialog.grab_set()
        
        tk.Label(dialog, text=f"Phân quyền cho người dùng:", font=("Segoe UI", 10)).pack(pady=(15, 5))
        tk.Label(dialog, text=username, font=("Segoe UI", 12, "bold"), fg="#1a73e8").pack(pady=5)
        
        var_read = tk.BooleanVar(value=("read" in current_perm))
        var_write = tk.BooleanVar(value=("write" in current_perm))
        var_share = tk.BooleanVar(value=("share" in current_perm))
        
        chk_frame = tk.Frame(dialog)
        chk_frame.pack(pady=10, padx=50, fill="x")
        
        tk.Checkbutton(chk_frame, text="Quyền Đọc (Read)", variable=var_read, font=("Segoe UI", 11)).pack(anchor="w")
        tk.Checkbutton(chk_frame, text="Quyền Ghi (Write)", variable=var_write, font=("Segoe UI", 11)).pack(anchor="w")
        tk.Checkbutton(chk_frame, text="Quyền Chia sẻ (Share)", variable=var_share, font=("Segoe UI", 11)).pack(anchor="w")
        
        result = {"perm": None}

        def on_ok():
            perms = []
            if var_read.get(): perms.append("read")
            if var_write.get(): perms.append("write")
            if var_share.get(): perms.append("share")
            
            if not perms:
                self.show_custom_message("Lỗi", "Vui lòng chọn ít nhất một quyền!", is_error=True, parent=dialog)
                return
            
            result["perm"] = ",".join(perms)
            dialog.destroy()

        btn_frame = tk.Frame(dialog)
        btn_frame.pack(pady=20)
        tk.Button(btn_frame, text="Xác nhận", bg="#4CAF50", fg="white", width=10, command=on_ok).pack(side=tk.LEFT, padx=10)
        tk.Button(btn_frame, text="Hủy", width=10, command=dialog.destroy).pack(side=tk.LEFT, padx=10)
        
        self.wait_window(dialog)
        return result["perm"]

if __name__ == "__main__":
    app = DriveGUI()
    app.mainloop()