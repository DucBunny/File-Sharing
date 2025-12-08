# File: net_utils.py
import ctypes
import os
import sys
import json

# --- CẤU HÌNH LOAD THƯ VIỆN C ---
# Tên thư viện vẫn là network_lib.so (được biên dịch từ nhiều file C)
lib_name = "./network_lib.so"
if os.name == 'nt':
    lib_name = "./network_lib.dll"

try:
    # Tải thư viện C
    c_net = ctypes.CDLL(lib_name)
except OSError:
    print(f"LỖI: Không tìm thấy thư viện '{lib_name}'. Hãy biên dịch code C trước.")
    sys.exit(1)

# --- Định nghĩa hàm C (được gom từ network_server, network_client và network_common) ---

# Hàm Server (network_server.c)
c_net.create_server_socket.argtypes = [ctypes.c_char_p, ctypes.c_int]
c_net.create_server_socket.restype = ctypes.c_int
c_net.accept_client.argtypes = [ctypes.c_int, ctypes.c_char_p]
c_net.accept_client.restype = ctypes.c_int

# Hàm Client (network_client.c)
c_net.connect_to_server.argtypes = [ctypes.c_char_p, ctypes.c_int]
c_net.connect_to_server.restype = ctypes.c_int

# Hàm Common/Transfer (network_common.c)
c_net.send_packet.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
c_net.send_packet.restype = ctypes.c_int
c_net.recv_packet.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
c_net.recv_packet.restype = ctypes.POINTER(ctypes.c_char)
c_net.free_mem.argtypes = [ctypes.POINTER(ctypes.c_char)]
c_net.close_socket.argtypes = [ctypes.c_int]

# Fix lỗi init_network trên Linux (chỉ cần thiết cho Windows)
if os.name == 'nt':
    try:
        # Nếu dùng network_logic2.c, cần hàm này trên Windows
        c_net.init_network() 
    except AttributeError:
        pass

# --- Wrapper Functions (Không đổi, chỉ gọi c_net) ---

def c_send_json(sock_fd, data_dict):
    """Gói gọn việc chuyển từ Dict -> JSON -> Bytes -> Gửi qua socket C."""
    try:
        json_str = json.dumps(data_dict, default=str)
        data_bytes = json_str.encode('utf-8')
        return c_net.send_packet(sock_fd, data_bytes, len(data_bytes)) != -1
    except Exception as e:
        print(f"[Net Error] Error sending JSON: {e}")
        return False

def c_send_bytes(sock_fd, data_bytes):
    """Gửi trực tiếp một chuỗi bytes qua socket C."""
    return c_net.send_packet(sock_fd, data_bytes, len(data_bytes)) != -1

def c_recv_json(sock_fd):
    """Nhận dữ liệu từ socket C, giải phóng bộ nhớ C, và chuyển Bytes -> JSON -> Dict."""
    out_len = ctypes.c_int(0)
    ptr = c_net.recv_packet(sock_fd, ctypes.byref(out_len))
    if not ptr or out_len.value == -1: 
        return None
    try:
        data = ctypes.string_at(ptr, out_len.value)
        return json.loads(data.decode('utf-8'))
    finally:
        c_net.free_mem(ptr)

def c_recv_bytes(sock_fd):
    """Nhận trực tiếp chuỗi bytes từ socket C và giải phóng bộ nhớ C."""
    out_len = ctypes.c_int(0)
    ptr = c_net.recv_packet(sock_fd, ctypes.byref(out_len))
    if not ptr or out_len.value == -1: 
        return None
    try:
        return ctypes.string_at(ptr, out_len.value)
    finally:
        c_net.free_mem(ptr)

def c_close_socket(sock_fd):
    """Đóng socket."""
    c_net.close_socket(sock_fd)

def c_create_server_socket(host, port):
    """Tạo socket server (Sử dụng network_server.c)."""
    return c_net.create_server_socket(host.encode('utf-8'), port)

def c_accept_client(server_fd):
    """Chấp nhận kết nối client (Sử dụng network_server.c)."""
    client_ip = ctypes.create_string_buffer(50)
    client_fd = c_net.accept_client(server_fd, client_ip)
    return client_fd, client_ip.value.decode('utf-8')

def c_connect_to_server(host, port):
    """Kết nối tới server (Sử dụng network_client.c)."""
    return c_net.connect_to_server(host.encode('utf-8'), port)