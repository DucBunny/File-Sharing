# File: server_logic.py
import threading
import os
import shutil
from net_utils import c_create_server_socket, c_accept_client, c_send_json, c_recv_json, c_recv_bytes, c_send_bytes, c_close_socket
from db_manager import DBManager, SERVER_ROOT

# --- CẤU HÌNH ---
HOST = '127.0.0.1'
PORT = 65432

class FileServer:
    def __init__(self):
        self.db = DBManager()
        self.server_fd = -1
        if not os.path.exists(SERVER_ROOT):
            os.makedirs(SERVER_ROOT)

    def start(self):
        self.server_fd = c_create_server_socket(HOST, PORT)
        if self.server_fd < 0:
            print("[-] Server khởi tạo thất bại.")
            return

        print(f"[*] Server running on {HOST}:{PORT}")
        while True:
            client_fd, client_ip = c_accept_client(self.server_fd)
            if client_fd >= 0:
                print(f"[+] Client connected from {client_ip}")
                t = threading.Thread(target=self.handle_client, args=(client_fd,))
                t.start()

    def handle_client(self, client_fd):
        current_user = None 
        try:
            while True:
                req = c_recv_json(client_fd)
                if not req: break
                
                cmd = req.get('command')
                res = {"status": "error", "message": "Unknown command"}
                print(f"[LOG] User {current_user['id'] if current_user else 'GUEST'} command: {cmd}")

                if cmd == 'LOGIN':
                    user = self.db.login(req['username'], req['password'])
                    if user:
                        current_user = user
                        res = {"status": "success", "message": f"Xin chào {user['full_name']}", "user_id": user['id']}
                    else:
                        res = {"status": "fail", "message": "Sai tên đăng nhập hoặc mật khẩu"}

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

                # Xử lý Upload file
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
                            self.db.delete_node(node_id) # Xóa node nếu lỗi truyền file
                            res = {"status": "error", "message": "Lỗi truyền file"}
                
                # Xử lý Download file
                elif cmd == 'DOWNLOAD_INIT':
                    node_id = req['node_id']
                    phy_path = os.path.join(SERVER_ROOT, str(node_id))
                    
                    # Cần kiểm tra quyền đọc ở đây, nhưng tạm thời bỏ qua để tập trung vào kiến trúc
                    
                    if os.path.exists(phy_path):
                        size = os.path.getsize(phy_path)
                        c_send_json(client_fd, {"status": "ready", "filesize": size})
                        with open(phy_path, 'rb') as f:
                            data = f.read()
                        c_send_bytes(client_fd, data)
                        continue # KHÔNG gửi phản hồi JSON ở cuối vòng lặp
                    else:
                        res = {"status": "error", "message": "File hỏng hoặc không tồn tại"}

                elif cmd == 'SHARE_NODE':
                    if current_user:
                        ok = self.db.share_node(req['node_id'], req['target_username'], req['permission'])
                        if ok: res = {"status": "success", "message": "Đã chia sẻ thành công"}
                        else: res = {"status": "fail", "message": "Người dùng không tồn tại"}
                
                elif cmd == 'RENAME_NODE':
                    node_id = req['node_id']
                    new_name = req['new_name']
                    if not current_user or not self.db.check_write_permission(node_id, current_user['id']):
                        res = {"status": "fail", "message": "Không có quyền sửa tên"}
                    elif self.db.rename_node(node_id, new_name):
                        res = {"status": "success", "message": "Đổi tên thành công"}
                    else:
                        res = {"status": "error", "message": "Lỗi CSDL khi đổi tên"}

                elif cmd == 'DELETE_NODE':
                    node_id = req['node_id']
                    
                    if not current_user or not self.db.check_write_permission(node_id, current_user['id']):
                        res = {"status": "fail", "message": "Không có quyền xóa"}
                    else:
                        node_info = self.db.get_node_details(node_id, current_user['id'])
                        
                        if node_info and node_info['type'] == 'file':
                            phy_path = os.path.join(SERVER_ROOT, str(node_id))
                            if os.path.exists(phy_path):
                                try: os.remove(phy_path)
                                except Exception as e: print(f"Lỗi xóa file vật lý: {e}")
                                        
                        if self.db.delete_node(node_id):
                            res = {"status": "success", "message": "Xóa thành công"}
                        else:
                            res = {"status": "error", "message": "Lỗi CSDL khi xóa"}

                elif cmd == 'COPY_NODE':
                    node_id = req['node_id']
                    target_parent_id = req.get('target_parent_id')
                    node_info = self.db.get_node_details(node_id, current_user['id'])
                    if not node_info:
                         res = {"status": "fail", "message": "Node gốc không tồn tại."}
                    else:
                        new_id = self.db.copy_node_recursive(node_id, current_user['id'], target_parent_id)
                        if new_id:
                            res = {"status": "success", "message": "Sao chép thành công."}
                        else:
                            res = {"status": "error", "message": "Lỗi sao chép trong DB hoặc file vật lý."}

                elif cmd == 'MOVE_NODE':
                    node_id = req['node_id']
                    target_parent_id = req.get('target_parent_id')
                    
                    if not current_user or not self.db.check_write_permission(node_id, current_user['id']):
                        res = {"status": "fail", "message": "Không có quyền di chuyển node này."}
                    else:
                        node_info = self.db.get_node_details(node_id, current_user['id'])
                        if not node_info:
                            res = {"status": "fail", "message": "Node không tồn tại."}
                        elif node_id == target_parent_id or (target_parent_id and node_info['parent_id'] == target_parent_id):
                            res = {"status": "fail", "message": "Thư mục đích không hợp lệ (Di chuyển vào chính nó hoặc vị trí cũ)."}
                        elif self.db.move_node(node_id, target_parent_id):
                            res = {"status": "success", "message": "Di chuyển thành công."}
                        else:
                            res = {"status": "error", "message": "Lỗi CSDL khi di chuyển."}

                c_send_json(client_fd, res)
        except Exception as e:
            print(f"Server Error: {e}")
        finally:
            c_close_socket(client_fd)
            print(f"[-] Client disconnected.")