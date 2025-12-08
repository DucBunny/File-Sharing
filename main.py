# File: main.py
import tkinter as tk
import threading
import time
import sys
import os

# Import các modules đã chia nhỏ
from server_logic import FileServer
from client_gui import DriveGUI
from db_manager import SERVER_ROOT # Import để kiểm tra thư mục gốc

if __name__ == "__main__":
    
    # 1. Khởi tạo và chạy Server trong luồng riêng
    # Đảm bảo thư mục lưu trữ tồn tại
    if not os.path.exists(SERVER_ROOT):
        os.makedirs(SERVER_ROOT)
        
    server = FileServer()
    server_thread = threading.Thread(target=server.start, daemon=True)
    
    try:
        server_thread.start()
        print(">> Bắt đầu Server...")
    except Exception as e:
        print(f"LỖI: Không thể khởi động Server: {e}")
        sys.exit(1)

    # Đợi server khởi động (1 giây)
    time.sleep(1) 
    
    # 2. Khởi tạo và chạy Client GUI
    root = tk.Tk()
    app = DriveGUI(root)
    root.mainloop()