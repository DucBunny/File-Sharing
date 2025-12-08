#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"
#include "network_logic.c" // Import code mạng tầng thấp

static socket_t g_sock = INVALID_SOCKET;
static int g_user_id = -1;

// --- HÀM CHO PYTHON GỌI ---

int cli_connect(const char *ip, int port)
{
    init_network();
    g_sock = connect_to_server(ip, port);
    return (g_sock != INVALID_SOCKET);
}

// Đăng nhập
int cli_login(const char *username, const char *password_hash, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_LOGIN;
    strcpy(req.username, username);
    strcpy(req.password, password_hash);

    if (send_packet(g_sock, (char *)&req, sizeof(req)) < 0)
        return -1;

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
        return -1;

    ResponsePacket res;
    memcpy(&res, data, sizeof(res));
    free(data);

    strcpy(out_msg, res.message);
    if (res.status == CMD_SUCCESS)
    {
        g_user_id = (int)res.data_val;
        return g_user_id;
    }
    return -1;
}

// Đăng ký (Mới)
int cli_register(const char *username, const char *pass_hash, const char *fullname, const char *email, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_REGISTER;
    strcpy(req.username, username);
    strcpy(req.password, pass_hash);
    strcpy(req.arg1, fullname); // arg1 dùng chứa Fullname
    strcpy(req.arg2, email);    // arg2 dùng chứa Email

    if (send_packet(g_sock, (char *)&req, sizeof(req)) < 0)
    {
        strcpy(out_msg, "Connection Error");
        return 0;
    }

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        strcpy(out_msg, "No Response");
        return 0;
    }

    ResponsePacket res;
    memcpy(&res, data, sizeof(res));
    free(data);

    strcpy(out_msg, res.message);
    return (res.status == CMD_SUCCESS);
}

// Tạo thư mục (Mới)
int cli_create_folder(const char *name, int parent_id, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_CREATE_FOLDER;
    req.user_id = g_user_id;
    req.parent_id = parent_id;
    strcpy(req.arg1, name); // arg1 chứa tên folder

    send_packet(g_sock, (char *)&req, sizeof(req));

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        strcpy(out_msg, "Network Error");
        return 0;
    }

    ResponsePacket res;
    memcpy(&res, data, sizeof(res));
    free(data);

    strcpy(out_msg, res.message);
    return (res.status == CMD_SUCCESS);
}

// Lấy danh sách file
FileInfo *cli_list_files(int parent_id, int *out_count)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_LIST;
    req.user_id = g_user_id;
    req.parent_id = parent_id;

    send_packet(g_sock, (char *)&req, sizeof(req));

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        *out_count = 0;
        return NULL;
    }

    int count = *(int *)data;
    free(data);
    *out_count = count;

    if (count <= 0)
        return NULL;

    FileInfo *files = (FileInfo *)malloc(sizeof(FileInfo) * count);
    for (int i = 0; i < count; i++)
    {
        data = recv_packet(g_sock, &len);
        if (data)
        {
            memcpy(&files[i], data, sizeof(FileInfo));
            free(data);
        }
    }
    return files;
}

// Upload file
int cli_upload(const char *filepath, const char *filename, long long filesize, int parent_id, char *out_msg)
{
    FILE *fp = fopen(filepath, "rb");
    if (!fp)
    {
        strcpy(out_msg, "Cannot open local file");
        return 0;
    }

    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_UPLOAD_INIT;
    req.user_id = g_user_id;
    req.parent_id = parent_id;
    req.size = filesize;
    strcpy(req.arg1, filename);

    send_packet(g_sock, (char *)&req, sizeof(req));

    // Chờ server báo Ready
    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        fclose(fp);
        return 0;
    }

    ResponsePacket res;
    memcpy(&res, data, sizeof(res));
    free(data);

    if (res.status != CMD_SUCCESS)
    {
        strcpy(out_msg, res.message);
        fclose(fp);
        return 0;
    }

    // Gửi stream bytes
    char buffer[4096];
    while (!feof(fp))
    {
        int n = fread(buffer, 1, 4096, fp);
        if (n > 0)
            send(g_sock, buffer, n, 0);
    }
    fclose(fp);

    // Chờ xác nhận cuối
    data = recv_packet(g_sock, &len);
    memcpy(&res, data, sizeof(res));
    free(data);

    strcpy(out_msg, res.message);
    return (res.status == CMD_SUCCESS);
}

void cli_free_files(FileInfo *ptr)
{
    if (ptr)
        free(ptr);
}