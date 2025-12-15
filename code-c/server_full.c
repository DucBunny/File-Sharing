#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <mysql/mysql.h>
#include <unistd.h> // cho unlink
#include "protocol.h"
#include <bits/pthreadtypes.h>
#include "network_logic.c"

#define SERVER_ROOT "server_storage"
#define DB_HOST "localhost"
#define DB_USER "fileuser"
#define DB_PASS "FilePassword123"
#define DB_NAME "file_system_db"
#define DB_PORT 33061

MYSQL *connect_db()
{
    MYSQL *conn = mysql_init(NULL);
    if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0) == NULL)
    {
        fprintf(stderr, "DB Connect Failed: %s\n", mysql_error(conn));
        return NULL;
    }
    return conn;
}

// Kiểm tra quyền sở hữu của node (Mới)
int check_ownership(MYSQL *conn, int node_id, int user_id)
{
    char query[256];
    sprintf(query, "SELECT id FROM nodes WHERE id = %d AND owner_id = %d", node_id, user_id);
    if (mysql_query(conn, query))
        return 0;
    MYSQL_RES *result = mysql_store_result(conn);
    int count = (result && mysql_num_rows(result) > 0);
    mysql_free_result(result);
    return count;
}

// Xóa file vật lý (Mới)
void delete_physical_file(long long file_id)
{
    char filepath[512];
    sprintf(filepath, "%s/%lld", SERVER_ROOT, file_id);
    unlink(filepath);
}

void handle_login(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    char query[512];
    sprintf(query, "SELECT id, full_name FROM users WHERE username='%s' AND password_hash='%s'", req->username, req->password);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Query Error");
    }
    else
    {
        MYSQL_RES *result = mysql_store_result(conn);
        if (result && mysql_num_rows(result) > 0)
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            res.status = CMD_SUCCESS;
            res.data_val = atoi(row[0]);
            sprintf(res.message, "Welcome %s", row[1]);
        }
        else
        {
            res.status = CMD_ERROR;
            strcpy(res.message, "Invalid credentials");
        }
        mysql_free_result(result);
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Đăng ký
void handle_register(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    char query[1024];
    sprintf(query, "INSERT INTO users (username, password_hash, full_name, email, role) VALUES ('%s', '%s', '%s', '%s', 'user')",
            req->username, req->password, req->arg1, req->arg2);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        // Kiểm tra lỗi trùng lặp
        if (mysql_errno(conn) == 1062)
            strcpy(res.message, "Username already exists");
        else
            strcpy(res.message, "DB Error");
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Register successful");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý tạo thư mục
void handle_create_folder(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    char parent_val[20] = "NULL";
    if (req->parent_id > 0)
        sprintf(parent_val, "%d", req->parent_id);

    char query[512];
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, '%s', 'folder', %s, 0)",
            req->user_id, req->arg1, parent_val);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Cannot create folder (DB)");
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Folder created");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// Hàm chung để gửi danh sách node (dùng cho LIST và SEARCH)
void send_node_list(int sock, MYSQL *conn, const char *query, int user_id)
{
    if (mysql_query(conn, query))
    {
        int count = 0;
        send_packet(sock, (char *)&count, sizeof(int));
        return;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    int count = mysql_num_rows(result);
    send_packet(sock, (char *)&count, sizeof(int));

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)))
    {
        FileInfo fi;
        fi.id = atoi(row[0]);
        strcpy(fi.name, row[1]);
        strcpy(fi.type, row[2]);
        fi.size = atoll(row[3]);
        strcpy(fi.owner, row[4]);
        fi.is_shared = atoi(row[5]);
        send_packet(sock, (char *)&fi, sizeof(fi));
    }
    mysql_free_result(result);
}

void handle_list(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[2048];
    // Root directory (parent_id = 0)
    if (req->parent_id == 0)
    {
        sprintf(query,
                "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
                "FROM nodes n JOIN users u ON n.owner_id = u.id "
                "WHERE n.owner_id = %d AND n.parent_id IS NULL "
                "UNION "
                "SELECT n.id, n.name, n.type, n.size, u.username, 1 as is_shared "
                "FROM shared_nodes sn JOIN nodes n ON sn.node_id = n.id "
                "JOIN users u ON n.owner_id = u.id WHERE sn.shared_with_user = %d",
                req->user_id, req->user_id);
    }
    else // Sub-directory
    {
        sprintf(query,
                "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
                "FROM nodes n JOIN users u ON n.owner_id = u.id "
                "WHERE n.owner_id = %d AND n.parent_id = %d",
                req->user_id, req->parent_id);
    }

    send_node_list(sock, conn, query, req->user_id);
}

// Xử lý tìm kiếm (Mới)
void handle_search(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[2048];
    // Tìm kiếm trong toàn bộ thư mục sở hữu và được chia sẻ (LIST/search_parent_id = 0)
    sprintf(query,
            "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
            "FROM nodes n JOIN users u ON n.owner_id = u.id "
            "WHERE n.owner_id = %d AND n.name LIKE '%%%s%%' "
            "UNION "
            "SELECT n.id, n.name, n.type, n.size, u.username, 1 as is_shared "
            "FROM shared_nodes sn JOIN nodes n ON sn.node_id = n.id "
            "JOIN users u ON n.owner_id = u.id WHERE sn.shared_with_user = %d AND n.name LIKE '%%%s%%'",
            req->user_id, req->arg1, req->user_id, req->arg1);

    send_node_list(sock, conn, query, req->user_id);
}

void handle_upload(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    char parent_val[20] = "NULL";
    if (req->parent_id > 0)
        sprintf(parent_val, "%d", req->parent_id);

    // KẾT THÚC: Thêm logic Upload/Download folder (Yêu cầu thư viện nén)

    char query[512];
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, '%s', 'file', %s, %lld)",
            req->user_id, req->arg1, parent_val, req->size);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error: Cannot create node");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    long long file_id = mysql_insert_id(conn);

    res.status = CMD_SUCCESS;
    // Gửi tín hiệu ready
    send_packet(sock, (char *)&res, sizeof(res));

    char filepath[512];
    sprintf(filepath, "%s/%lld", SERVER_ROOT, file_id);
    FILE *fp = fopen(filepath, "wb");

    long long remaining = req->size;
    char buffer[4096];
    while (remaining > 0)
    {
        int to_read = (remaining > 4096) ? 4096 : (int)remaining;
        int n = recv(sock, buffer, to_read, 0);
        if (n <= 0)
            break;
        if (fp)
            fwrite(buffer, 1, n, fp);
        remaining -= n;
    }
    if (fp)
        fclose(fp);

    res.status = CMD_SUCCESS;
    strcpy(res.message, "Upload OK");
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Tải xuống
void handle_download(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    char query[512];
    // Lấy thông tin file (bao gồm kiểm tra quyền sở hữu/chia sẻ)
    sprintf(query, "SELECT n.type, n.size FROM nodes n WHERE n.id = %d AND (n.owner_id = %d OR EXISTS (SELECT 1 FROM shared_nodes sn WHERE sn.node_id = n.id AND sn.shared_with_user = %d AND sn.permission LIKE 'r%%'))",
            req->node_id, req->user_id, req->user_id);

    if (mysql_query(conn, query))
        goto db_error;

    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        strcpy(res.message, "File not found or access denied.");
        goto send_error;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    // KẾT THÚC: Thêm logic Upload/Download folder (Yêu cầu thư viện nén)
    if (strcmp(row[0], "file") != 0) // Chỉ cho download file
    {
        mysql_free_result(result);
        strcpy(res.message, "Cannot download a folder (Compression required).");
        goto send_error;
    }

    long long filesize = atoll(row[1]);
    mysql_free_result(result);

    // Gửi Ready tín hiệu và kích thước file
    res.status = CMD_SUCCESS;
    res.data_val = filesize;
    send_packet(sock, (char *)&res, sizeof(res));

    // Bắt đầu gửi file
    char filepath[512];
    sprintf(filepath, "%s/%d", SERVER_ROOT, req->node_id);
    FILE *fp = fopen(filepath, "rb");

    if (!fp)
    {
        strcpy(res.message, "Server file missing.");
        res.status = CMD_ERROR;
        // Gửi gói tin lỗi để báo client dừng
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char buffer[4096];
    size_t bytes_read;
    // Dùng send_all để đảm bảo toàn bộ dữ liệu được gửi
    while ((bytes_read = fread(buffer, 1, 4096, fp)) > 0)
    {
        if (send_all(sock, buffer, bytes_read) == -1)
        {
            // Lỗi gửi mạng
            fprintf(stderr, "Error sending file chunk.\n");
            break;
        }
    }
    fclose(fp);
    return;

db_error:
    strcpy(res.message, "DB Query Error.");
send_error:
    res.status = CMD_ERROR;
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Đổi tên
void handle_rename(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!check_ownership(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied or node not found.");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char query[512];
    sprintf(query, "UPDATE nodes SET name = '%s' WHERE id = %d AND owner_id = %d",
            req->arg1, req->node_id, req->user_id);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error: Cannot rename node.");
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Rename successful.");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Xóa
void handle_delete(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    char query[512];
    long long file_id = 0;

    // 1. Kiểm tra quyền sở hữu và lấy type/size (chỉ file)
    sprintf(query, "SELECT type FROM nodes WHERE id = %d AND owner_id = %d", req->node_id, req->user_id);
    if (mysql_query(conn, query))
        goto db_error;

    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        strcpy(res.message, "Access denied or node not found.");
        goto send_error_free_res;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    char node_type[10];
    strcpy(node_type, row[0]);
    mysql_free_result(result);

    // 2. Xóa file vật lý nếu là FILE
    if (strcmp(node_type, "file") == 0)
    {
        file_id = req->node_id;
        delete_physical_file(file_id);
    }
    // LƯU Ý: Nếu là folder, các file con sẽ không bị xóa vật lý,
    // cần logic đệ quy để xóa file vật lý của folder (Bỏ qua trong bản này)

    // 3. Xóa node khỏi DB (ON DELETE CASCADE sẽ xóa cả shared_nodes và con cháu (nếu là folder))
    sprintf(query, "DELETE FROM nodes WHERE id = %d AND owner_id = %d", req->node_id, req->user_id);
    if (mysql_query(conn, query))
        goto db_error;

    res.status = CMD_SUCCESS;
    strcpy(res.message, "Delete successful.");
    send_packet(sock, (char *)&res, sizeof(res));
    return;

db_error:
    strcpy(res.message, "DB Query Error during delete.");
send_error_free_res:
    if (result)
        mysql_free_result(result);
send_error:
    res.status = CMD_ERROR;
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Sao chép
void handle_copy(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    char query[1024];

    // 1. Lấy thông tin node gốc
    sprintf(query, "SELECT name, type, size, owner_id FROM nodes WHERE id = %d AND owner_id = %d",
            req->node_id, req->user_id);
    if (mysql_query(conn, query))
        goto db_error;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        strcpy(res.message, "Node not found or access denied.");
        goto send_error_free_res;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    char name[256], type[10];
    long long size;
    strcpy(name, row[0]);
    strcpy(type, row[1]);
    size = atoll(row[2]);
    mysql_free_result(result);

    if (strcmp(type, "folder") == 0)
    {
        strcpy(res.message, "Folder copy not supported yet.");
        goto send_error;
    }

    // 2. Kiểm tra và Tạo node mới trong DB
    char parent_val[20] = "NULL";

    if (req->parent_id > 0)
    {
        // VALIDATION: Check if target parent exists and is a folder
        sprintf(query, "SELECT type FROM nodes WHERE id = %d", req->parent_id);
        if (mysql_query(conn, query))
        {
            strcpy(res.message, "DB Error during target validation.");
            goto send_error;
        }

        MYSQL_RES *target_result = mysql_store_result(conn);
        if (!target_result || mysql_num_rows(target_result) == 0)
        {
            if (target_result)
                mysql_free_result(target_result);
            strcpy(res.message, "Target parent folder not found or is invalid.");
            goto send_error;
        }

        MYSQL_ROW target_row = mysql_fetch_row(target_result);
        if (strcmp(target_row[0], "folder") != 0)
        {
            mysql_free_result(target_result);
            strcpy(res.message, "Target ID is not a folder.");
            goto send_error;
        }
        mysql_free_result(target_result);
        sprintf(parent_val, "%d", req->parent_id);
    }

    // 3. Thực hiện INSERT
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, 'Copy of %s', 'file', %s, %lld)",
            req->user_id, name, parent_val, size);

    if (mysql_query(conn, query))
        goto db_error;
    long long new_id = mysql_insert_id(conn);

    // 4. Sao chép file vật lý
    char src_path[512], dest_path[512];
    sprintf(src_path, "%s/%d", SERVER_ROOT, req->node_id);
    sprintf(dest_path, "%s/%lld", SERVER_ROOT, new_id);

    FILE *src = fopen(src_path, "rb");
    FILE *dest = fopen(dest_path, "wb");

    if (src && dest)
    {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0)
        {
            fwrite(buffer, 1, n, dest);
        }
        fclose(src);
        fclose(dest);
        res.status = CMD_SUCCESS;
        sprintf(res.message, "Copy of %s successful.", name);
    }
    else
    {
        // Rollback DB
        sprintf(query, "DELETE FROM nodes WHERE id = %lld", new_id);
        mysql_query(conn, query);
        res.status = CMD_ERROR;
        strcpy(res.message, "File copy failed on server.");
    }
    send_packet(sock, (char *)&res, sizeof(res));
    return;

db_error:
    strcpy(res.message, "DB Query Error during copy.");
send_error_free_res:
    if (result)
        mysql_free_result(result);
send_error:
    res.status = CMD_ERROR;
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Di chuyển
void handle_move(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!check_ownership(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied or node not found.");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char parent_val[20] = "NULL";
    char query[512];

    if (req->parent_id > 0)
    {
        // 1. Kiểm tra node đích có tồn tại và là folder không
        sprintf(query, "SELECT type FROM nodes WHERE id = %d", req->parent_id);
        if (mysql_query(conn, query))
        {
            res.status = CMD_ERROR;
            strcpy(res.message, "DB Error during target validation.");
            send_packet(sock, (char *)&res, sizeof(res));
            return;
        }

        MYSQL_RES *result = mysql_store_result(conn);
        if (!result || mysql_num_rows(result) == 0)
        {
            if (result)
                mysql_free_result(result);
            res.status = CMD_ERROR;
            strcpy(res.message, "Target parent folder not found or is invalid.");
            send_packet(sock, (char *)&res, sizeof(res));
            return;
        }

        MYSQL_ROW row = mysql_fetch_row(result);
        if (strcmp(row[0], "folder") != 0)
        {
            mysql_free_result(result);
            res.status = CMD_ERROR;
            strcpy(res.message, "Target ID is not a folder.");
            send_packet(sock, (char *)&res, sizeof(res));
            return;
        }
        mysql_free_result(result);
        sprintf(parent_val, "%d", req->parent_id);
    }

    // Thực hiện move
    sprintf(query, "UPDATE nodes SET parent_id = %s WHERE id = %d AND owner_id = %d",
            parent_val, req->node_id, req->user_id);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error: Cannot move node.");
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Move successful.");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// Xử lý Chia sẻ (ĐÃ KIỂM TRA LẠI LOGIC)
void handle_share(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    char query[512];

    // 1. Kiểm tra node có thuộc sở hữu của người dùng không (dùng check_ownership)
    if (!check_ownership(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "You can only share your own files/folders.");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    // 2. Tìm ID của người dùng nhận chia sẻ (req->arg1 là target_username)
    sprintf(query, "SELECT id FROM users WHERE username = '%s'", req->arg1);
    if (mysql_query(conn, query))
        goto db_error;

    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        res.status = CMD_ERROR;
        sprintf(res.message, "User '%s' not found.", req->arg1);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    int target_user_id = atoi(row[0]);
    mysql_free_result(result);

    // 2b. Ngăn chặn chia sẻ với chính mình
    if (target_user_id == req->user_id)
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Cannot share file with yourself.");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    // 3. Thực hiện chia sẻ (req->arg2 là permission)
    // SỬA: Thêm UNIQUE index vào (node_id, shared_with_user) trong DB để tránh lỗi insert trùng
    sprintf(query, "INSERT INTO shared_nodes (node_id, shared_with_user, permission) VALUES (%d, %d, '%s') ON DUPLICATE KEY UPDATE permission = VALUES(permission)",
            req->node_id, target_user_id, req->arg2);

    if (mysql_query(conn, query))
        goto db_error;

    res.status = CMD_SUCCESS;
    sprintf(res.message, "Shared node successfully with %s.", req->arg1);
    send_packet(sock, (char *)&res, sizeof(res));
    return;

db_error:
    strcpy(res.message, "DB Query Error during share.");
    res.status = CMD_ERROR;
    send_packet(sock, (char *)&res, sizeof(res));
}

void *client_thread(void *arg)
{
    int sock = *(int *)arg;
    free(arg);
    MYSQL *conn = connect_db();
    if (!conn)
    {
        close_socket(sock);
        return NULL;
    }

    RequestPacket req;
    int len;
    while (1)
    {
        char *data = recv_packet(sock, &len);
        if (!data)
            break;
        memcpy(&req, data, sizeof(RequestPacket));
        free(data);

        switch (req.command)
        {
        case CMD_LOGIN:
            handle_login(sock, conn, &req);
            break;
        case CMD_REGISTER:
            handle_register(sock, conn, &req);
            break;
        case CMD_CREATE_FOLDER:
            handle_create_folder(sock, conn, &req);
            break;
        case CMD_LIST:
            handle_list(sock, conn, &req);
            break;
        case CMD_SEARCH:
            handle_search(sock, conn, &req);
            break;
        case CMD_UPLOAD_INIT:
            handle_upload(sock, conn, &req);
            break;
        case CMD_DOWNLOAD_INIT:
            handle_download(sock, conn, &req);
            break;
        case CMD_RENAME:
            handle_rename(sock, conn, &req);
            break;
        case CMD_DELETE:
            handle_delete(sock, conn, &req);
            break;
        case CMD_COPY:
            handle_copy(sock, conn, &req);
            break;
        case CMD_MOVE:
            handle_move(sock, conn, &req);
            break;
        case CMD_SHARE:
            handle_share(sock, conn, &req);
            break;
        }
    }
    mysql_close(conn);
    close_socket(sock);
    return NULL;
}

int main()
{
    init_network();
#ifdef _WIN32
    _mkdir(SERVER_ROOT);
#else
    mkdir(SERVER_ROOT, 0777);
#endif

    int server_sock = create_server_socket("0.0.0.0", 65432);
    if (server_sock < 0)
        return 1;
    printf("C Server running on 65432...\n");

    while (1)
    {
        char ip[50];
        int client_sock = accept_client(server_sock, ip);
        if (client_sock >= 0)
        {
            int *new_sock = malloc(sizeof(int));
            *new_sock = client_sock;
#ifdef _WIN32
            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)client_thread, new_sock, 0, NULL);
#else
            pthread_t tid;
            pthread_create(&tid, NULL, client_thread, new_sock);
            pthread_detach(tid);
#endif
        }
    }
    return 0;
}