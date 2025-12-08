#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// Các mã lệnh (Command Codes)
#define CMD_LOGIN 1
#define CMD_REGISTER 2
#define CMD_LIST 3
#define CMD_UPLOAD_INIT 4
#define CMD_DOWNLOAD_INIT 5
#define CMD_SHARE 6
#define CMD_CREATE_FOLDER 7
#define CMD_ERROR 99
#define CMD_SUCCESS 100

// Cấu trúc gói tin Request (Client gửi lên Server)
typedef struct
{
    int command;
    char username[50];
    char password[65]; // Hash sha256
    char arg1[256];    // Filename, Folder name, Fullname
    char arg2[256];    // Email, Target user
    long long size;    // Filesize
    int user_id;       // ID người dùng gửi yêu cầu
    int parent_id;     // ID thư mục cha (0 nếu là root)
} RequestPacket;

// Cấu trúc gói tin Response (Server trả về)
typedef struct
{
    int status;         // CMD_SUCCESS hoặc CMD_ERROR
    char message[256];  // Thông báo
    long long data_val; // Chứa ID mới tạo hoặc dữ liệu số khác
} ResponsePacket;

// Cấu trúc thông tin File (dùng cho lệnh LIST)
typedef struct
{
    int id;
    char name[256];
    char type[10]; // "file" or "folder"
    long long size;
    char owner[50];
    int is_shared;
} FileInfo;

#endif