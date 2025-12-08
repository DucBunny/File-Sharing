#!/bin/bash

echo "--- BẮT ĐẦU THIẾT LẬP HỆ THỐNG FILE SHARING ---"

# 1. Kiểm tra và cài đặt dependencies (cần quyền sudo)
echo "[1/5] Kiểm tra và cài đặt các gói hệ thống cần thiết (Bao gồm Tkinter)..."

# Cài đặt các gói cần thiết: MySQL, Build Tools, Python Dev, và đặc biệt là Tkinter
# Sử dụng apt-get install trực tiếp để đảm bảo cài đặt
sudo apt-get update
sudo apt-get install -y mysql-server libmysqlclient-dev build-essential python3-tk python3-pip

if ! python3 -c "import tkinter" &> /dev/null; then
    echo "LỖI Tkinter vẫn còn. Vui lòng đảm bảo rằng lệnh 'sudo apt-get install python3-tk' đã chạy thành công."
    # Cố gắng cài lại một lần nữa phòng trường hợp bị lỗi
    sudo apt-get install -y python3-tk
    if [ $? -ne 0 ]; then
        echo "LỖỖI: Không thể cài đặt Tkinter. Vui lòng kiểm tra kết nối mạng và repository."
        exit 1
    fi
fi
echo " -> Các gói hệ thống đã được cài đặt."


# 2. Cài thư viện Python
echo "[2/5] Cài đặt thư viện Python (mysql-connector-python)..."
pip3 install mysql-connector-python --break-system-packages 2>/dev/null || pip3 install mysql-connector-python
if [ $? -ne 0 ]; then
    echo "LỖI: Không thể cài đặt thư viện Python 'mysql-connector-python'. Vui lòng kiểm tra pip."
    exit 1
fi
echo " -> Thư viện Python đã được cài đặt."

# 3. Biên dịch Code C (CẬP NHẬT: Biên dịch nhiều file)
echo "[3/5] Biên dịch Network Library (C) từ network_server.c, network_client.c và network_common.c..."

# Kiểm tra sự tồn tại của các file C mới
if [ ! -f "network_server.c" ] || [ ! -f "network_client.c" ] || [ ! -f "network_common.c" ]; then
    echo "LỖI: Không tìm thấy các file C mới (network_server.c, network_client.c, network_common.c)."
    exit 1
fi

# Lệnh biên dịch 3 file C thành 1 thư viện dùng chung network_lib.so
gcc -shared -o network_lib.so -fPIC network_server.c network_client.c network_common.c
if [ $? -eq 0 ]; then
    echo " -> Biên dịch thành công: network_lib.so"
else
    echo " -> Lỗi biên dịch C! Vui lòng kiểm tra lỗi trong các file C."
    exit 1
fi

# 4. Thiết lập Cơ sở dữ liệu (Yêu cầu TƯƠNG TÁC NGƯỜI DÙNG)
echo "[4/5] Thiết lập Cơ sở dữ liệu (tạo DB, User 'fileuser', các Bảng)..."
if [ -f "setup_db.sql" ]; then
    # Khởi động dịch vụ MySQL (Nếu chưa chạy)
    sudo systemctl start mysql
    
    echo "=========================================================================="
    echo "!!! CẦN THIẾT LẬP THỦ CÔNG: BỎ QUA LỖI 'ERROR 1045' TỰ ĐỘNG !!!"
    echo "Vui lòng chạy lệnh sau trong một Terminal khác hoặc chạy thủ công để thiết lập DB:"
    echo ""
    echo "   mysql -u root -p < setup_db.sql"
    echo ""
    echo "Sau khi chạy lệnh trên và nhập mật khẩu MySQL ROOT thành công, gõ 'y' để tiếp tục."
    echo "=========================================================================="

    read -r -p "Bạn đã chạy lệnh thiết lập CSDL thành công chưa? (y/n) " response
    if [[ "$response" != "y" ]]; then
        echo "LỖI: Vui lòng thiết lập CSDL thủ công trước khi chạy ứng dụng."
        exit 1
    fi
    echo " -> Xác nhận Thiết lập Cơ sở dữ liệu đã hoàn tất."
else
    echo "LỖI: Không tìm thấy file 'setup_db.sql'."
    exit 1
fi


# 5. Khởi chạy ứng dụng
echo "[5/5] Đang khởi chạy ứng dụng..."
if [ -f "main.py" ]; then
    python3 main.py
else
    echo "LỖI: Không tìm thấy file 'main.py'."
    exit 1
fi