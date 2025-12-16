#!/bin/bash

echo "--- BẮT ĐẦU THIẾT LẬP HỆ THỐNG FILE SHARING ---"

# 1. Kiểm tra và cài đặt dependencies (cần quyền sudo)
echo "[1/4] Kiểm tra các gói cần thiết..."
if ! command -v mysql &> /dev/null; then
    echo "MySQL chưa được cài. Đang cài đặt..."
    sudo apt-get update
    sudo apt-get install -y mysql-server libmysqlclient-dev build-essential python3-tk python3-pip
fi

# 2. Cài thư viện Python
echo "[2/4] Cài đặt thư viện Python..."
pip3 install mysql-connector-python --break-system-packages 2>/dev/null || pip3 install mysql-connector-python

# 3. Biên dịch Code C
echo "[3/4] Biên dịch Network Library (C)..."
if [ -f "network_logic.c" ]; then
    gcc -shared -o network_lib.so -fPIC network_logic.c
    if [ $? -eq 0 ]; then
        echo " -> Biên dịch thành công: network_lib.so"
    else
        echo " -> Lỗi biên dịch C!"
        exit 1
    fi
else
    echo "LỖI: Không tìm thấy file 'network_logic.c'. Hãy tạo file này trước!"
    exit 1
fi

# 4. Khởi chạy ứng dụng
echo "[4/4] Đang khởi chạy ứng dụng..."
if [ -f "file_sharing_main.py" ]; then
    python3 file_sharing_main.py
else
    echo "LỖI: Không tìm thấy file 'file_sharing_main.py'."
fi