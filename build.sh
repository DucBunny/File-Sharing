#!/bin/bash

echo "--- BẮT ĐẦU THIẾT LẬP HỆ THỐNG FILE SHARING ---"

# 1. Kiểm tra dependencies
echo "[1/7] Kiểm tra tất cả các gói cần thiết; nếu thiếu sẽ tự cài..."
REQUIRED_PACKAGES=(mysql-server libmysqlclient-dev build-essential python3-tk python3-pip zip gcc)
MISSING=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING+=("$pkg")
    fi
done
if [ ${#MISSING[@]} -ne 0 ]; then
    echo "Gói thiếu: ${MISSING[*]}. Đang cài đặt..."
    sudo apt-get update
    sudo apt-get install -y "${MISSING[@]}"
else
    echo "Đã cài tất cả các gói cần thiết."
fi

# 2. Cài thư viện Python
echo "[2/7] Cài đặt thư viện Python..."
pip3 install mysql-connector-python pillow --break-system-packages \
|| pip3 install mysql-connector-python pillow

# 3. Biên dịch Server
echo "[3/7] Biên dịch Server..."
if [ -f "server/server_main.c" ]; then
    gcc -o server/server_full \
        server/server_main.c \
        server/server_logging.c \
        server/server_db.c \
        server/server_permissions.c \
        server/server_file_ops.c \
        server/server_handlers.c \
        -lmysqlclient -lpthread
    echo " -> Đã biên dịch Server"
else
    echo "LỖI: Không tìm thấy server/server_main.c"
    exit 1
fi

# 4. Biên dịch Client
echo "[4/7] Biên dịch Client..."
if [ -f "client/client_logic.c" ]; then
    gcc -shared -fPIC -o client/client_logic.so client/client_logic.c
    echo " -> Đã biên dịch Client Library"
else
    echo "LỖI: Không tìm thấy client/client_logic.c"
    exit 1
fi

# 5. Biên dịch thư viện 
echo "[5/7] Biên dịch thư viện network..."
if [ -f "common/network_logic.c" ]; then
    gcc -shared -fPIC -o common/network_lib.so common/network_logic.c
    echo " -> Đã biên dịch Network Library"
else
    echo "LỖI: Không tìm thấy common/network_logic.c"
    exit 1
fi

# 6. Database
read -p "[6/7] Khởi tạo lại Database? (y/n): " db_choice
if [ "$db_choice" = "y" ]; then
    echo "Nhập mật khẩu MySQL root"
    sudo mysql -u root -p < server/setup_db.sql
fi

# 7. Hướng dẫn chạy chương trình
echo "[7/7] HƯỚNG DẪN CHẠY ỨNG DỤNG FILE SHARING"
echo ""
echo "Cách 1: Chạy Server"
echo "   cd server"
echo "   ./server_full"
echo ""
echo "Cách 2: Chạy Client GUI"
echo "   cd client"
echo "   python3 file_sharing_main.py"
echo ""
echo "Cách 3: Chạy cả hai (khuyến nghị)"
echo "   Mở terminal 1:"
echo "     cd server"
echo "     ./server_full"
echo ""
echo "   Mở terminal 2:"
echo "     cd client"
echo "     python3 file_sharing_main.py"
echo ""
echo "--- HOÀN TẤT THIẾT LẬP ---"