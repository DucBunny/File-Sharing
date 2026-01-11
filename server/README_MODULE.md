# Cấu trúc Server Module - File Sharing System

## Tổng quan
Server được tái cấu trúc thành các module riêng biệt theo chức năng.

## Cấu trúc Module

### 1. server_logging.c/h
**Chức năng:** Quản lý logging
- `write_log()` - Ghi log vào file với timestamp
- Thread-safe với mutex

### 2. server_db.c/h
**Chức năng:** Thao tác cơ sở dữ liệu
- `connect_db()` - Kết nối MySQL database
- `get_username_by_id()` - Lấy tên user từ ID
- `get_node_name_by_id()` - Lấy tên node từ ID
- `check_ownership()` - Kiểm tra quyền sở hữu
- `check_is_shared()` - Kiểm tra trạng thái chia sẻ
- `get_unique_name()` - Tạo tên duy nhất (tránh trùng lặp)

### 3. server_permissions.c/h
**Chức năng:** Quản lý quyền truy cập
- `check_has_share_permission()` - Kiểm tra quyền chia sẻ
- `check_access_recursive()` - Kiểm tra quyền truy cập đệ quy
- `has_write_access()` - Kiểm tra quyền ghi
- `has_read_access()` - Kiểm tra quyền đọc
- `share_recursive()` - Chia sẻ đệ quy
- `remove_share_recursive()` - Xóa chia sẻ đệ quy

### 4. server_file_ops.c/h
**Chức năng:** Thao tác file vật lý
- `delete_physical_file()` - Xóa file vật lý
- `copy_physical_file()` - Sao chép file vật lý
- `copy_recursive_inner()` - Sao chép đệ quy (folder)
- `recursive_copy_for_zip()` - Sao chép để tạo ZIP

### 5. server_handlers.c/h
**Chức năng:** Xử lý các request từ client
- `handle_login()` - Đăng nhập
- `handle_register()` - Đăng ký
- `handle_create_folder()` - Tạo folder
- `handle_list()` - Liệt kê file/folder
- `handle_search()` - Tìm kiếm
- `handle_get_path()` - Lấy đường dẫn
- `handle_upload()` - Upload file
- `handle_download()` - Download file
- `handle_rename()` - Đổi tên
- `handle_delete()` - Xóa
- `handle_copy()` - Sao chép
- `handle_move()` - Di chuyển
- `handle_share()` - Chia sẻ
- `handle_get_share_list()` - Lấy danh sách chia sẻ
- `handle_remove_share()` - Xóa chia sẻ

### 6. server_main.c
**Chức năng:** Logic chính của server
- Khởi tạo server
- Quản lý kết nối
- Thread pool cho clients
- Signal handling

## Biên dịch

```bash
./build.sh
```

Hoặc thủ công:
```bash
gcc -o server/server_full \
    server/server_main.c \
    server/server_logging.c \
    server/server_db.c \
    server/server_permissions.c \
    server/server_file_ops.c \
    server/server_handlers.c \
    -lmysqlclient -lpthread
```

## Chạy Server

```bash
cd server
./server_full
```