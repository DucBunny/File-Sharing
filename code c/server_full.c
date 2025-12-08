#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <mysql/mysql.h>
#include "protocol.h"
#include <bits/pthreadtypes.h>
#include "network_logic.c"

#define SERVER_ROOT "server_storage"
#define DB_HOST "localhost"
#define DB_USER "fileuser"
#define DB_PASS "FilePassword123"
#define DB_NAME "file_system_db"

MYSQL *connect_db()
{
    MYSQL *conn = mysql_init(NULL);
    if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0) == NULL)
    {
        fprintf(stderr, "DB Connect Failed: %s\n", mysql_error(conn));
        return NULL;
    }
    return conn;
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
        strcpy(res.message, "Cannot create folder");
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Folder created");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_list(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[2048];
    if (req->parent_id == 0 || req->parent_id == -1)
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
    else
    {
        sprintf(query,
                "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
                "FROM nodes n JOIN users u ON n.owner_id = u.id "
                "WHERE n.owner_id = %d AND n.parent_id = %d",
                req->user_id, req->parent_id);
    }

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

void handle_upload(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    char parent_val[20] = "NULL";
    if (req->parent_id > 0)
        sprintf(parent_val, "%d", req->parent_id);

    char query[512];
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, '%s', 'file', %s, %lld)",
            req->user_id, req->arg1, parent_val, req->size);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    long long file_id = mysql_insert_id(conn);

    res.status = CMD_SUCCESS;
    send_packet(sock, (char *)&res, sizeof(res));

    char filepath[512];
    sprintf(filepath, "%s/%lld", SERVER_ROOT, file_id);
    FILE *fp = fopen(filepath, "wb");

    long long remaining = req->size;
    char buffer[4096];
    while (remaining > 0)
    {
        int to_read = (remaining > 4096) ? 4096 : remaining;
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
        case CMD_UPLOAD_INIT:
            handle_upload(sock, conn, &req);
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