# File: run_server.py
import os
import sys
from server_logic import FileServer
from db_manager import SERVER_ROOT # Import để kiểm tra thư mục gốc

if __name__ == "__main__":
    
    # Đảm bảo thư mục lưu trữ file vật lý tồn tại
    if not os.path.exists(SERVER_ROOT):
        os.makedirs(SERVER_ROOT)
        
    server = FileServer()
    print(">> BẮT ĐẦU FILE SHARING SERVER <<")
    print(f"Server Host: 127.0.0.1, Port: 65432")
    print(f"MySQL Port: 33061 (Kiểm tra db_manager.py)")
    print("Nhấn Ctrl+C để dừng Server.")
    
    try:
        server.start()
    except KeyboardInterrupt:
        print("\n[Server] Đã nhận lệnh Ctrl+C. Đang tắt Server...")
        # Các logic dọn dẹp (nếu có) sẽ được thêm vào đây
        sys.exit(0)
    except Exception as e:
        print(f"[LỖI] Server gặp lỗi nghiêm trọng: {e}")
        sys.exit(1)