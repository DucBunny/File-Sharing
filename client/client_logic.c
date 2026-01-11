#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/protocol.h"
#include "../common/network_logic.c"
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>

#define BUFFER_SIZE 8192

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
    strcpy(req.arg1, name);

    send_packet(g_sock, (char *)&req, sizeof(req));

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        strcpy(out_msg, "Network Error");
        return -1;
    }

    ResponsePacket res;
    // Xử lý an toàn kích thước
    size_t copy_size = (len < sizeof(ResponsePacket)) ? len : sizeof(ResponsePacket);
    memcpy(&res, data, copy_size);
    free(data);

    strcpy(out_msg, res.message);

    if (res.status == CMD_SUCCESS)
    {
        // Trả về ID của folder mới (Server phải gửi cái này trong data_val)
        return (int)res.data_val;
    }
    return -1; // Thất bại
}

// Lấy danh sách file (Dùng cho LIST và SEARCH)
FileInfo *cli_get_nodes(int command, int search_parent_id, const char *search_query, int *out_count)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = command;
    req.user_id = g_user_id;
    req.parent_id = search_parent_id;
    if (search_query)
    {
        strcpy(req.arg1, search_query);
    }

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

// Hàm gọi cho LIST
FileInfo *cli_list_files(int parent_id, int *out_count)
{
    return cli_get_nodes(CMD_LIST, parent_id, NULL, out_count);
}

// Hàm gọi cho SEARCH
FileInfo *cli_search_files(const char *search_query, int *out_count)
{
    return cli_get_nodes(CMD_SEARCH, 0, search_query, out_count);
}

// Lấy thông tin đường dẫn
PathNode *cli_get_node_path(int node_id, int *out_count)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_GET_PATH;
    req.user_id = g_user_id;
    req.node_id = node_id;

    send_packet(g_sock, (char *)&req, sizeof(req));

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        *out_count = 0;
        return NULL;
    }

    // Gói tin đầu tiên chứa count (số lượng node trên đường dẫn)
    int count = *(int *)data;
    free(data);
    *out_count = count;

    if (count <= 0)
        return NULL;

    // Các gói tin tiếp theo chứa PathNode
    PathNode *path = (PathNode *)malloc(sizeof(PathNode) * count);
    for (int i = 0; i < count; i++)
    {
        data = recv_packet(g_sock, &len);
        if (data)
        {
            memcpy(&path[i], data, sizeof(PathNode));
            free(data);
        }
    }
    return path;
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
    // Dùng CMD_UPLOAD_INIT cho cả file thường và file zip (thư mục)
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

// Download file
int cli_download(int node_id, const char *local_filepath, char *out_msg)
{
    // 1. Gửi Request
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_DOWNLOAD_INIT;
    req.user_id = g_user_id;
    req.node_id = node_id;

    if (send_packet(g_sock, (char *)&req, sizeof(req)) <= 0)
    {
        snprintf(out_msg, 256, "Send Request Failed");
        return 0;
    }

    // 2. Nhận Header
    int len = 0;
    char *data = recv_packet(g_sock, &len); // len là tổng số byte nhận được
    if (!data)
    {
        snprintf(out_msg, 256, "Network Error: Cannot receive header");
        return 0;
    }

    size_t min_required = sizeof(int) + sizeof(int) + sizeof(long long);
    if (len < min_required)
    {
        free(data);
        snprintf(out_msg, 256, "Invalid packet size (%d bytes)", len);
        return 0;
    }

    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    // Copy Header
    size_t header_size = sizeof(ResponsePacket);
    size_t bytes_to_copy = (len < header_size) ? len : header_size;
    memcpy(&res, data, bytes_to_copy);

    if (res.status != CMD_SUCCESS)
    {
        free(data);
        snprintf(out_msg, 256, "Server Error: %.240s", res.message);
        return 0;
    }

    long long filesize = res.data_val;

    // 3. Mở file
    FILE *fp = fopen(local_filepath, "wb");
    if (!fp)
    {
        free(data);
        snprintf(out_msg, 256, "Cannot open local file: %s", local_filepath);
        return 0;
    }

    // --- XỬ LÝ DỮ LIỆU THỪA TỪ GÓI HEADER ---
    long long remaining = filesize;
    long long total_received = 0;

    // Nếu recv_packet lỡ đọc lấn sang nội dung file
    if (len > header_size)
    {
        int extra_len = len - header_size;
        // Ghi phần thừa vào file ngay lập tức
        fwrite(data + header_size, 1, extra_len, fp);

        remaining -= extra_len;
        total_received += extra_len;

        // Debug output removed to avoid terminal prints
    }

    free(data); // Bây giờ mới được free

    // 4. Thiết lập Timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    // 5. Vòng lặp nhận phần còn lại
    char buffer[8192];

    while (remaining > 0)
    {
        int to_read = (remaining > 8192) ? 8192 : (int)remaining;

        int n = recv(g_sock, buffer, to_read, 0);

        if (n < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                snprintf(out_msg, 256, "Timeout: Server stopped sending data.");
            else
                snprintf(out_msg, 256, "Socket Error.");
            break;
        }
        else if (n == 0)
        {
            snprintf(out_msg, 256, "Connection closed unexpectedly.");
            break;
        }

        fwrite(buffer, 1, n, fp);
        remaining -= n;
        total_received += n;
    }

    fclose(fp);

    // Reset Timeout
    tv.tv_sec = 0;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);

    if (total_received == filesize)
    {
        snprintf(out_msg, 256, "Download OK");
        return 1;
    }
    else
    {
        if (strlen(out_msg) == 0)
            snprintf(out_msg, 256, "Incomplete: %lld/%lld bytes", total_received, filesize);
        return 0;
    }
}

// Đổi tên file/folder
int cli_rename(int node_id, const char *new_name, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_RENAME;
    req.user_id = g_user_id;
    req.node_id = node_id;
    strcpy(req.arg1, new_name);

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

// Xóa file/folder
int cli_delete(int node_id, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_DELETE;
    req.user_id = g_user_id;
    req.node_id = node_id;

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

// Sao chép file/folder
int cli_copy(int node_id, int target_parent_id, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_COPY;
    req.user_id = g_user_id;
    req.node_id = node_id;
    req.parent_id = target_parent_id; // parent_id: thư mục đích

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

// Di chuyển file/folder
int cli_move(int node_id, int target_parent_id, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_MOVE;
    req.user_id = g_user_id;
    req.node_id = node_id;
    req.parent_id = target_parent_id; // parent_id: thư mục đích

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

// Chia sẻ node (Mới)
int cli_share_node(int node_id, const char *target_username, const char *permission, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_SHARE;
    req.user_id = g_user_id;
    req.node_id = node_id;
    strcpy(req.arg1, target_username); // arg1: tên người dùng nhận
    strcpy(req.arg2, permission);      // arg2: quyền (read, write)

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

void cli_free_files(FileInfo *ptr)
{
    if (ptr)
        free(ptr);
}

// Hàm để Python giải phóng bộ nhớ cho PathNode
void cli_free_path(PathNode *ptr)
{
    if (ptr)
        free(ptr);
}

// Hàm lấy danh sách chia sẻ
ShareEntry *cli_get_share_list(int node_id, int *count_out)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_GET_SHARE_LIST;
    req.user_id = g_user_id;
    req.node_id = node_id;

    send_packet(g_sock, (char *)&req, sizeof(req));

    int len;
    char *data = recv_packet(g_sock, &len);
    if (!data)
    {
        *count_out = 0;
        return NULL;
    }

    int count = *(int *)data;
    free(data);
    *count_out = count;

    if (count == 0)
        return NULL;

    ShareEntry *list = malloc(count * sizeof(ShareEntry));
    for (int i = 0; i < count; i++)
    {
        data = recv_packet(g_sock, &len);
        memcpy(&list[i], data, sizeof(ShareEntry));
        free(data);
    }
    return list;
}

void cli_free_share_list(ShareEntry *list)
{
    if (list)
        free(list);
}

// Hàm gỡ chia sẻ
int cli_remove_share(int node_id, const char *username, char *out_msg)
{
    RequestPacket req;
    memset(&req, 0, sizeof(req));
    req.command = CMD_REMOVE_SHARE;
    req.user_id = g_user_id;
    req.node_id = node_id;
    strcpy(req.arg1, username);

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