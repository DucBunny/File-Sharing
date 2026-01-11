#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// ============================================================
// ĐỊNH NGHĨA CÁC LỆNH (COMMAND CODES)
// ============================================================
#define CMD_LOGIN 1
#define CMD_REGISTER 2
#define CMD_LIST 3
#define CMD_UPLOAD_INIT 4
#define CMD_DOWNLOAD_INIT 5
#define CMD_SHARE 6        // Chia sẻ node
#define CMD_CREATE_FOLDER 7
#define CMD_RENAME 8
#define CMD_DELETE 9
#define CMD_COPY 10
#define CMD_MOVE 11
#define CMD_SEARCH 12      // Tìm kiếm node
#define CMD_GET_PATH 13    // Lấy đường dẫn của một node
#define CMD_GET_SHARE_LIST 14  // MỚI: Lấy danh sách chia sẻ
#define CMD_REMOVE_SHARE 15    // MỚI: Gỡ quyền chia sẻ
#define CMD_ERROR 99
#define CMD_SUCCESS 100

// ============================================================
// CẤU TRÚC DỮ LIỆU (STRUCTS)
// Sử dụng #pragma pack(1) để đảm bảo không có byte đệm (padding)
// ============================================================

#pragma pack(push, 1) // Bắt đầu ép kiểu 1 byte alignment

// 1. Cấu trúc gói tin Request (Client gửi lên Server)
typedef struct
{
    int command;
    char username[50];
    char password[65]; // Hash sha256
    char arg1[256];    // Filename, Folder name, Fullname, New Name, Search Query...
    char arg2[256];    // Email, Target user (Share Permission)...
    long long size;    // Filesize
    int user_id;       // ID người dùng gửi yêu cầu
    int parent_id;     // ID thư mục cha (0 nếu là root)
    int node_id;       // ID của node bị tác động
} RequestPacket;

// 2. Cấu trúc gói tin Response (Server trả về)
typedef struct
{
    int status;         // CMD_SUCCESS hoặc CMD_ERROR
    char message[256];  // Thông báo
    long long data_val; // Chứa ID mới tạo, kích thước file, hoặc dữ liệu số khác
} ResponsePacket;

// 3. Cấu trúc thông tin File (dùng cho lệnh LIST/SEARCH)
typedef struct
{
    int id;
    char name[256];
    char type[10];      // "file" or "folder"
    long long size;
    char owner[50];
    int is_shared;
} FileInfo;

// 4. Cấu trúc Node trên đường dẫn (dùng cho lệnh GET_PATH)
typedef struct
{
    int id;
    char name[256];
} PathNode;

// 5. Permission
typedef struct {
    char username[50];
    char permission[50]; 
} ShareEntry;

#pragma pack(pop) // Kết thúc ép kiểu, trả về cấu hình mặc định

#endif // PROTOCOL_H