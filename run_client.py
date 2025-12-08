# File: run_client.py
import tkinter as tk
import sys
import time
from client_gui import DriveGUI

if __name__ == "__main__":
    print(">> BẮT ĐẦU FILE SHARING CLIENT GUI <<")
    print("Đang cố gắng kết nối tới Server...")

    # Chạy Client GUI
    root = tk.Tk()
    app = DriveGUI(root)
    
    # Kiểm tra kết nối sau khi khởi tạo GUI
    if app.connect():
        print("[Client] Kết nối Server thành công.")
    else:
        print("[Client] LỖI: Không thể kết nối tới Server. Vui lòng chạy Server trước.")
        messagebox.showerror("Lỗi kết nối", "Không thể kết nối Server. Vui lòng đảm bảo Server đang chạy.")
        sys.exit(1)

    root.mainloop()