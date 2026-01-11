#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <mysql/mysql.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "../common/protocol.h"
#include "../common/network_logic.c"
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include "../common/config.h"

// =============================================================
// 1. LOGGING & DATABASE HELPERS
// =============================================================

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t stop_server = 0;
static int server_socket_fd = -1;

// Ghi log vào file, KHÔNG in ra terminal
void write_log(const char *format, ...)
{
    pthread_mutex_lock(&log_mutex);

    FILE *fp = fopen("server_log.txt", "a");
    if (fp)
    {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d] ",
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                t->tm_hour, t->tm_min, t->tm_sec);

        va_list args;
        va_start(args, format);
        vfprintf(fp, format, args);
        va_end(args);

        fprintf(fp, "\n");
        fclose(fp);
    }

    pthread_mutex_unlock(&log_mutex);
}

void handle_termination_signal(int signum)
{
    (void)signum;
    stop_server = 1;
    // Write a newline so the shell prompt appears on a new line after Ctrl+C
    const char nl = '\n';
    write(STDOUT_FILENO, &nl, 1);
    if (server_socket_fd >= 0)
    {
        close(server_socket_fd);
        server_socket_fd = -1;
    }
}

MYSQL *connect_db()
{
    MYSQL *conn = mysql_init(NULL);
    if (mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0) == NULL)
    {
        fprintf(stderr, "LỖI DATABASE: Không thể kết nối: %s\n", mysql_error(conn));
        write_log("DB_ERROR: Connect Failed: %s", mysql_error(conn));
        return NULL;
    }
    return conn;
}

int check_ownership(MYSQL *conn, int node_id, int user_id)
{
    char query[256];
    sprintf(query, "SELECT id FROM nodes WHERE id = %d AND owner_id = %d", node_id, user_id);
    if (mysql_query(conn, query))
        return 0;
    MYSQL_RES *result = mysql_store_result(conn);
    int count = (result && mysql_num_rows(result) > 0);
    if (result)
        mysql_free_result(result);
    return count;
}

int check_has_share_permission(MYSQL *conn, int node_id, int user_id)
{
    if (node_id <= 0)
        return 0;

    char query[256];

    sprintf(query,
            "SELECT 1 FROM nodes WHERE id=%d AND owner_id=%d",
            node_id, user_id);

    if (mysql_query(conn, query) == 0)
    {
        MYSQL_RES *r = mysql_store_result(conn);
        if (r && mysql_num_rows(r) > 0)
        {
            mysql_free_result(r);
            return 1;
        }
        if (r)
            mysql_free_result(r);
    }

    sprintf(query,
            "SELECT permission FROM shared_nodes "
            "WHERE node_id=%d AND shared_with_user=%d",
            node_id, user_id);

    if (mysql_query(conn, query) == 0)
    {
        MYSQL_RES *r = mysql_store_result(conn);
        if (r && mysql_num_rows(r) > 0)
        {
            MYSQL_ROW row = mysql_fetch_row(r);
            if (row[0] && strstr(row[0], "share"))
            {
                mysql_free_result(r);
                return 1;
            }
        }
        if (r)
            mysql_free_result(r);
    }

    return 0;
}

void get_username_by_id(MYSQL *conn, int user_id, char *out_name)
{
    char query[256];
    sprintf(query, "SELECT username FROM users WHERE id=%d", user_id);
    if (mysql_query(conn, query) == 0)
    {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res && mysql_num_rows(res) > 0)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            strcpy(out_name, row[0]);
        }
        else
        {
            strcpy(out_name, "Unknown");
        }
        if (res)
            mysql_free_result(res);
    }
    else
    {
        strcpy(out_name, "Error");
    }
}

void get_node_name_by_id(MYSQL *conn, int node_id, char *out_name)
{
    if (node_id == 0)
    {
        strcpy(out_name, "Root");
        return;
    }
    char query[256];
    sprintf(query, "SELECT name FROM nodes WHERE id=%d", node_id);
    if (mysql_query(conn, query) == 0)
    {
        MYSQL_RES *res = mysql_store_result(conn);
        if (res && mysql_num_rows(res) > 0)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            strcpy(out_name, row[0]);
        }
        else
        {
            strcpy(out_name, "Unknown File");
        }
        if (res)
            mysql_free_result(res);
    }
    else
    {
        strcpy(out_name, "Error");
    }
}

void delete_physical_file(long long file_id)
{
    char filepath[512];
    sprintf(filepath, "%s/%lld", SERVER_ROOT, file_id);
    unlink(filepath);
}

// =============================================================
// 2. LOGIC NÂNG CAO (COPY, SHARE, UNIQUE NAME)
// =============================================================

void get_unique_name(MYSQL *conn, int parent_id, char *original_name, char *out_name, int is_folder)
{
    char base_name[256];
    char extension[50];
    strcpy(out_name, original_name);

    strcpy(base_name, original_name);
    strcpy(extension, "");

    if (!is_folder)
    {
        char *dot = strrchr(original_name, '.');
        if (dot)
        {
            int base_len = dot - original_name;
            strncpy(base_name, original_name, base_len);
            base_name[base_len] = '\0';
            strcpy(extension, dot);
        }
    }

    int counter = 1;
    while (1)
    {
        char query[512];
        char parent_val[20] = "NULL";
        if (parent_id > 0)
            sprintf(parent_val, "%d", parent_id);

        sprintf(query, "SELECT id FROM nodes WHERE parent_id %s %s AND name='%s'",
                (parent_id > 0 ? "=" : "IS"), parent_val, out_name);

        if (mysql_query(conn, query))
            break;
        MYSQL_RES *res = mysql_store_result(conn);
        if (res == NULL)
            break;
        int row_count = mysql_num_rows(res);
        mysql_free_result(res);

        if (row_count == 0)
            break;

        sprintf(out_name, "%s (%d)%s", base_name, counter, extension);
        counter++;
        if (counter > 1000)
            break;
    }
}

int copy_physical_file(int src_id, int dest_id)
{
    char src_path[256], dest_path[256];
    sprintf(src_path, "%s/%d", SERVER_ROOT, src_id);
    sprintf(dest_path, "%s/%d", SERVER_ROOT, dest_id);

    FILE *f_src = fopen(src_path, "rb");
    if (!f_src)
        return 0;

    FILE *f_dest = fopen(dest_path, "wb");
    if (!f_dest)
    {
        fclose(f_src);
        return 0;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), f_src)) > 0)
    {
        fwrite(buffer, 1, bytes, f_dest);
    }

    fclose(f_src);
    fclose(f_dest);
    return 1;
}

void copy_recursive_inner(MYSQL *conn, int src_parent_id, int dest_parent_id, int owner_id)
{
    char query[512];
    sprintf(query, "SELECT id, name, type, size FROM nodes WHERE parent_id=%d", src_parent_id);
    if (mysql_query(conn, query))
        return;

    MYSQL_RES *res = mysql_store_result(conn);
    if (!res)
        return;

    int num_rows = mysql_num_rows(res);
    if (num_rows == 0)
    {
        mysql_free_result(res);
        return;
    }

    struct NodeInfo
    {
        int id;
        char name[256];
        char type[10];
        long long size;
    };
    struct NodeInfo *nodes = malloc(num_rows * sizeof(struct NodeInfo));

    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)))
    {
        nodes[i].id = atoi(row[0]);
        strcpy(nodes[i].name, row[1]);
        strcpy(nodes[i].type, row[2]);
        nodes[i].size = atoll(row[3]);
        i++;
    }
    mysql_free_result(res);

    for (int k = 0; k < num_rows; k++)
    {
        char insert_q[1024];
        sprintf(insert_q, "INSERT INTO nodes (name, type, size, owner_id, parent_id, created_at) VALUES ('%s', '%s', %lld, %d, %d, NOW())",
                nodes[k].name, nodes[k].type, nodes[k].size, owner_id, dest_parent_id);

        if (mysql_query(conn, insert_q) == 0)
        {
            int new_child_id = mysql_insert_id(conn);
            if (strcmp(nodes[k].type, "file") == 0)
            {
                copy_physical_file(nodes[k].id, new_child_id);
            }
            else if (strcmp(nodes[k].type, "folder") == 0)
            {
                copy_recursive_inner(conn, nodes[k].id, new_child_id, owner_id);
            }
        }
    }
    free(nodes);
}

void recursive_copy_for_zip(MYSQL *conn, int node_id, const char *base_path)
{
    char query[1024];
    sprintf(query, "SELECT id, name, type FROM nodes WHERE parent_id = %d", node_id);

    if (mysql_query(conn, query))
        return;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result)
        return;

    typedef struct
    {
        int id;
        char name[256];
        char type[10];
    } NodeInfo;
    NodeInfo *nodes = NULL;
    int count = mysql_num_rows(result);

    if (count > 0)
    {
        nodes = malloc(count * sizeof(NodeInfo));
        MYSQL_ROW row;
        int i = 0;
        while ((row = mysql_fetch_row(result)))
        {
            nodes[i].id = atoi(row[0]);
            strcpy(nodes[i].name, row[1]);
            strcpy(nodes[i].type, row[2]);
            i++;
        }
    }
    mysql_free_result(result);

    for (int i = 0; i < count; i++)
    {
        char new_path[1024];
        sprintf(new_path, "%s/%s", base_path, nodes[i].name);

        if (strcmp(nodes[i].type, "folder") == 0)
        {
#ifdef _WIN32
            _mkdir(new_path);
#else
            mkdir(new_path, 0777);
#endif
            recursive_copy_for_zip(conn, nodes[i].id, new_path);
        }
        else
        {
            char src_path[512];
            sprintf(src_path, "%s/%d", SERVER_ROOT, nodes[i].id);
            char cmd[2048];
            sprintf(cmd, "cp \"%s\" \"%s\"", src_path, new_path);
            system(cmd);
        }
    }
    if (nodes)
        free(nodes);
}

void share_recursive(MYSQL *conn, int node_id, int target_user_id, const char *permission)
{
    char query[1024];
    sprintf(query, "INSERT INTO shared_nodes (node_id, shared_with_user, permission) VALUES (%d, %d, '%s') ON DUPLICATE KEY UPDATE permission=VALUES(permission)", node_id, target_user_id, permission);
    mysql_query(conn, query);

    sprintf(query, "SELECT id FROM nodes WHERE parent_id = %d", node_id);
    if (mysql_query(conn, query))
        return;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result)
        return;

    int count = mysql_num_rows(result);
    if (count > 0)
    {
        int *child_ids = (int *)malloc(count * sizeof(int));
        int i = 0;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
            child_ids[i++] = atoi(row[0]);
        mysql_free_result(result);
        for (int k = 0; k < count; k++)
            share_recursive(conn, child_ids[k], target_user_id, permission);
        free(child_ids);
    }
    else
    {
        mysql_free_result(result);
    }
}

int check_is_shared(MYSQL *conn, int node_id, int user_id)
{
    char query[512];
    sprintf(query, "SELECT 1 FROM shared_nodes WHERE node_id = %d AND shared_with_user = %d", node_id, user_id);
    if (mysql_query(conn, query))
        return 0;
    MYSQL_RES *res = mysql_store_result(conn);
    int exists = (res && mysql_num_rows(res) > 0);
    if (res)
        mysql_free_result(res);
    return exists;
}

void remove_share_recursive(MYSQL *conn, int node_id, int target_user_id)
{
    char query[512];
    sprintf(query, "DELETE FROM shared_nodes WHERE node_id=%d AND shared_with_user=%d", node_id, target_user_id);
    mysql_query(conn, query);

    sprintf(query, "SELECT id FROM nodes WHERE parent_id = %d", node_id);
    if (mysql_query(conn, query))
        return;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result)
        return;

    int count = mysql_num_rows(result);
    if (count > 0)
    {
        int *child_ids = (int *)malloc(count * sizeof(int));
        int i = 0;
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
            child_ids[i++] = atoi(row[0]);
        mysql_free_result(result);
        for (int k = 0; k < count; k++)
            remove_share_recursive(conn, child_ids[k], target_user_id);
        free(child_ids);
    }
    else
    {
        mysql_free_result(result);
    }
}

int check_access_recursive(MYSQL *conn, int node_id, int user_id)
{
    if (node_id == 0)
        return 0;
    char query[512];
    sprintf(query, "SELECT 1 FROM shared_nodes WHERE node_id = %d AND shared_with_user = %d", node_id, user_id);
    if (mysql_query(conn, query))
        return 0;
    MYSQL_RES *res = mysql_store_result(conn);
    if (res && mysql_num_rows(res) > 0)
    {
        mysql_free_result(res);
        return 1;
    }
    if (res)
        mysql_free_result(res);

    sprintf(query, "SELECT parent_id, owner_id FROM nodes WHERE id = %d", node_id);
    if (mysql_query(conn, query))
        return 0;
    res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0)
    {
        if (res)
            mysql_free_result(res);
        return 0;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int parent_id = (row[0]) ? atoi(row[0]) : 0;
    int owner_id = atoi(row[1]);
    mysql_free_result(res);

    if (owner_id == user_id)
        return 1;
    return check_access_recursive(conn, parent_id, user_id);
}

// Kiểm tra quyền ghi (owner hoặc shared với quyền chứa "write")
int has_write_access(MYSQL *conn, int node_id, int user_id)
{
    if (node_id == 0)
        return 1;

    if (node_id < 0)
        return 0;

    int cur = node_id;
    while (cur > 0)
    {
        char q[256];
        sprintf(q, "SELECT owner_id, parent_id FROM nodes WHERE id=%d", cur);
        if (mysql_query(conn, q))
            return 0;

        MYSQL_RES *r = mysql_store_result(conn);
        if (!r || mysql_num_rows(r) == 0)
            return 0;

        MYSQL_ROW row = mysql_fetch_row(r);
        int owner = atoi(row[0]);
        int parent = row[1] ? atoi(row[1]) : 0;
        mysql_free_result(r);

        if (owner == user_id)
            return 1;

        sprintf(q,
                "SELECT permission FROM shared_nodes "
                "WHERE node_id=%d AND shared_with_user=%d",
                cur, user_id);

        if (mysql_query(conn, q) == 0)
        {
            r = mysql_store_result(conn);
            if (r && mysql_num_rows(r) > 0)
            {
                row = mysql_fetch_row(r);
                if (row[0] && strstr(row[0], "write"))
                {
                    mysql_free_result(r);
                    return 1;
                }
            }
            if (r)
                mysql_free_result(r);
        }

        cur = parent; // kế thừa từ cha
    }
    return 0;
}

// Kiểm tra quyền đọc (owner hoặc shared với quyền chứa "read")
int has_read_access(MYSQL *conn, int node_id, int user_id)
{
    if (node_id == 0)
        return 1;

    if (node_id < 0)
        return 0;

    int cur = node_id;
    while (cur > 0)
    {
        char q[256];
        sprintf(q, "SELECT owner_id, parent_id FROM nodes WHERE id=%d", cur);
        if (mysql_query(conn, q))
            return 0;

        MYSQL_RES *r = mysql_store_result(conn);
        if (!r || mysql_num_rows(r) == 0)
            return 0;

        MYSQL_ROW row = mysql_fetch_row(r);
        int owner = atoi(row[0]);
        int parent = row[1] ? atoi(row[1]) : 0;
        mysql_free_result(r);

        if (owner == user_id)
            return 1;

        sprintf(q,
                "SELECT permission FROM shared_nodes "
                "WHERE node_id=%d AND shared_with_user=%d",
                cur, user_id);

        if (mysql_query(conn, q) == 0)
        {
            r = mysql_store_result(conn);
            if (r && mysql_num_rows(r) > 0)
            {
                row = mysql_fetch_row(r);
                if (row[0] && strstr(row[0], "read"))
                {
                    mysql_free_result(r);
                    return 1;
                }
            }
            if (r)
                mysql_free_result(r);
        }

        cur = parent; // kế thừa từ cha
    }
    return 0;
}

// =============================================================
// 3. HANDLERS
// =============================================================

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
        write_log("LOGIN_ERR: DB Error for user '%s': %s", req->username, mysql_error(conn));
    }
    else
    {
        MYSQL_RES *result = mysql_store_result(conn);
        if (result && mysql_num_rows(result) > 0)
        {
            MYSQL_ROW row = mysql_fetch_row(result);
            res.status = CMD_SUCCESS;
            res.data_val = atoi(row[0]); // user_id
            write_log("LOGIN_SUCCESS: User '%s' (ID: %d) logged in.", req->username, res.data_val);
            snprintf(res.message, sizeof(res.message), "Welcome %s", row[1]);
        }
        else
        {
            res.status = CMD_ERROR;
            strcpy(res.message, "Invalid credentials");
            write_log("LOGIN_FAIL: Invalid credentials for username '%s'.", req->username);
        }
        mysql_free_result(result);
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

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
        if (mysql_errno(conn) == 1062)
        {
            strcpy(res.message, "Username exists");
            write_log("REGISTER_FAIL: Username '%s' already exists.", req->username);
        }
        else
        {
            strcpy(res.message, "DB Error");
            write_log("REGISTER_ERR: DB Error for '%s': %s", req->username, mysql_error(conn));
        }
    }
    else
    {
        res.status = CMD_SUCCESS;
        write_log("REGISTER_SUCCESS: New user '%s' registered.", req->username);
        strcpy(res.message, "Register successful");
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// [FIX] Log hiển thị tên Parent thay vì ID
void handle_create_folder(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    int is_upload = 0;
    int file_count = 0;
    char raw_name[256];
    strncpy(raw_name, req->arg1, 255);
    raw_name[255] = '\0';

    char *suffix = strstr(raw_name, "__UPLOAD_REQ__");
    if (suffix)
    {
        if (strlen(suffix) > 14)
            file_count = atoi(suffix + 14);
        *suffix = '\0';
        is_upload = 1;
    }

    char final_name[256];
    get_unique_name(conn, req->parent_id, raw_name, final_name, 1);

    // Chuẩn bị thông tin hiển thị
    char username[50];
    get_username_by_id(conn, req->user_id, username);
    char parent_name[256];
    get_node_name_by_id(conn, req->parent_id, parent_name);

    // Chặn tạo folder nếu không có quyền ghi trên parent
    if (!has_write_access(conn, req->parent_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        write_log("CREATE_FOLDER_DENY: User '%s' (ID %d) lacks write permission on Parent '%s' (ID %d) to create '%s'.",
                  username, req->user_id, parent_name, req->parent_id, final_name);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char parent_val[20] = "NULL";
    if (req->parent_id > 0)
        sprintf(parent_val, "%d", req->parent_id);

    char query[512];
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, '%s', 'folder', %s, 0)",
            req->user_id, final_name, parent_val);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        write_log("CREATE_FOLDER_ERR: User '%s' (ID %d) failed to create folder '%s' in Parent '%s' (ID %d). DB Error: %s",
                  username, req->user_id, final_name, parent_name, req->parent_id, mysql_error(conn));
    }
    else
    {
        res.status = CMD_SUCCESS;
        res.data_val = mysql_insert_id(conn);

        if (is_upload)
        {
            write_log("UPLOAD_FOLDER_SUCCESS: User '%s' (ID %d) uploaded folder '%s' (ID %d) containing %d files to Parent '%s' (ID %d).",
                      username, req->user_id, final_name, res.data_val, file_count, parent_name, req->parent_id);
        }
        else
        {
            write_log("CREATE_FOLDER_SUCCESS: User '%s' (ID %d) created folder '%s' (ID %d) in Parent '%s' (ID %d).",
                      username, req->user_id, final_name, res.data_val, parent_name, req->parent_id);
        }

        snprintf(res.message, sizeof(res.message), "Folder created: %.200s", final_name);
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_list(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[4096];
    int folder_id = req->parent_id;
    if (folder_id == 0 && req->node_id > 0)
        folder_id = req->node_id;

    if (folder_id == 0)
    { // ROOT
        sprintf(query,
                "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
                "FROM nodes n JOIN users u ON n.owner_id = u.id "
                "WHERE n.owner_id = %d AND n.parent_id IS NULL "
                "UNION "
                "SELECT n.id, n.name, n.type, n.size, u.username, 1 as is_shared "
                "FROM shared_nodes sn JOIN nodes n ON sn.node_id = n.id JOIN users u ON n.owner_id = u.id "
                "WHERE sn.shared_with_user = %d AND (n.parent_id IS NULL OR n.parent_id NOT IN (SELECT node_id FROM shared_nodes WHERE shared_with_user = %d))",
                req->user_id, req->user_id, req->user_id);
    }
    else
    { // FOLDER CON
        if (check_access_recursive(conn, folder_id, req->user_id))
        {
            sprintf(query,
                    "SELECT n.id, n.name, n.type, n.size, u.username, 0 as is_shared "
                    "FROM nodes n JOIN users u ON n.owner_id = u.id "
                    "WHERE n.parent_id = %d",
                    folder_id);
        }
        else
        {
            int count = 0;
            send_packet(sock, (char *)&count, sizeof(int));
            return;
        }
    }

    if (mysql_query(conn, query))
    {
        write_log("LIST_ERR: User ID %d failed list folder %d. DB Error: %s", req->user_id, folder_id, mysql_error(conn));
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
        memset(&fi, 0, sizeof(fi));
        fi.id = atoi(row[0]);
        strncpy(fi.name, row[1], 255);
        strncpy(fi.type, row[2], 9);
        fi.size = atoll(row[3]);
        strncpy(fi.owner, row[4], 49);
        fi.is_shared = atoi(row[5]);
        send_packet(sock, (char *)&fi, sizeof(fi));
    }
    mysql_free_result(result);
}

void handle_search(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[2048];
    sprintf(query, "SELECT n.id, n.name, n.type, n.size, u.username, n.owner_id FROM nodes n JOIN users u ON n.owner_id = u.id WHERE n.name LIKE '%%%s%%'", req->arg1);

    if (mysql_query(conn, query))
    {
        write_log("SEARCH_ERR: User ID %d search '%s' failed. DB Error: %s", req->user_id, req->arg1, mysql_error(conn));
        int count = 0;
        send_packet(sock, (char *)&count, sizeof(int));
        return;
    }
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result)
    {
        int count = 0;
        send_packet(sock, (char *)&count, sizeof(int));
        return;
    }

    int num_rows = mysql_num_rows(result);
    char *valid_rows = (char *)calloc(num_rows, sizeof(char));
    int valid_count = 0;

    MYSQL_ROW row;
    int i = 0;
    while ((row = mysql_fetch_row(result)))
    {
        if (check_access_recursive(conn, atoi(row[0]), req->user_id))
        {
            valid_rows[i] = 1;
            valid_count++;
        }
        i++;
    }
    send_packet(sock, (char *)&valid_count, sizeof(int));
    mysql_data_seek(result, 0);

    i = 0;
    while ((row = mysql_fetch_row(result)))
    {
        if (valid_rows[i] == 1)
        {
            FileInfo fi;
            memset(&fi, 0, sizeof(fi));
            fi.id = atoi(row[0]);
            strncpy(fi.name, row[1], 255);
            strncpy(fi.type, row[2], 9);
            fi.size = atoll(row[3]);
            strncpy(fi.owner, row[4], 49);
            fi.is_shared = (atoi(row[5]) != req->user_id) ? 1 : 0;
            send_packet(sock, (char *)&fi, sizeof(fi));
        }
        i++;
    }
    free(valid_rows);
    mysql_free_result(result);
}

void handle_get_path(int sock, MYSQL *conn, RequestPacket *req)
{
    int current = req->node_id;
    PathNode path[100];
    int count = 0;
    char query[512];

    while (current > 0 && count < 100)
    {
        sprintf(query, "SELECT name, parent_id FROM nodes WHERE id = %d", current);
        if (mysql_query(conn, query))
            break;
        MYSQL_RES *res = mysql_store_result(conn);
        if (!res || mysql_num_rows(res) == 0)
        {
            if (res)
                mysql_free_result(res);
            break;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        path[count].id = current;
        strcpy(path[count].name, row[0]);
        int pid = (row[1]) ? atoi(row[1]) : 0;
        mysql_free_result(res);

        if (!check_ownership(conn, current, req->user_id))
        {
            int is_cur = check_is_shared(conn, current, req->user_id);
            int is_par = (pid > 0) ? check_is_shared(conn, pid, req->user_id) : 0;
            if (is_cur && !is_par)
            {
                count++;
                break;
            }
        }
        count++;
        current = pid;
    }

    PathNode reversed[100];
    for (int i = 0; i < count; i++)
        reversed[i] = path[count - 1 - i];
    send_packet(sock, (char *)&count, sizeof(int));
    for (int i = 0; i < count; i++)
        send_packet(sock, (char *)&reversed[i], sizeof(PathNode));
}

// [FIX] Log hiển thị tên Parent thay vì ID
void handle_upload(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;

    int silent = 0;
    char raw_name[256];
    strncpy(raw_name, req->arg1, 255);
    raw_name[255] = '\0';

    char *suffix = strstr(raw_name, "__NO_LOG__");
    if (suffix)
    {
        *suffix = '\0';
        silent = 1;
    }

    char final_name[256];
    get_unique_name(conn, req->parent_id, raw_name, final_name, 0);

    // Kiểm tra quyền ghi trên parent
    char username[50];
    get_username_by_id(conn, req->user_id, username);
    char parent_name[256];
    get_node_name_by_id(conn, req->parent_id, parent_name);
    if (!has_write_access(conn, req->parent_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        write_log("UPLOAD_DENY: User '%s' (ID %d) lacks write permission on Parent '%s' (ID %d) to upload '%s'.",
                  username, req->user_id, parent_name, req->parent_id, final_name);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char parent_val[20] = "NULL";
    if (req->parent_id > 0)
        sprintf(parent_val, "%d", req->parent_id);

    char query[512];
    sprintf(query, "INSERT INTO nodes (owner_id, name, type, parent_id, size) VALUES (%d, '%s', 'file', %s, %lld)", req->user_id, final_name, parent_val, req->size);
    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        write_log("UPLOAD_ERR: User '%s' (ID %d) failed to upload file '%s' to Parent '%s' (ID %d). DB Error: %s",
                  username, req->user_id, final_name, parent_name, req->parent_id, mysql_error(conn));
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

    if (!silent)
    {
        write_log("UPLOAD_FILE_SUCCESS: User '%s' (ID %d) uploaded file '%s' (ID %lld, Size %lld) to Parent '%s' (ID %d).",
                  username, req->user_id, final_name, file_id, req->size, parent_name, req->parent_id);
    }

    res.status = CMD_SUCCESS;
    snprintf(res.message, sizeof(res.message), "Upload saved as '%.200s'", final_name);
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_download(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!has_read_access(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log(
            "DOWNLOAD_DENY: user=%s(%d) lacks read permission on node_id=(%d) to download",
            username,
            req->user_id,
            req->node_id);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    int is_temp_zip = 0;

    char query[512];
    sprintf(query, "SELECT type, name FROM nodes WHERE id = %d", req->node_id);
    if (mysql_query(conn, query))
        goto send_error;
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("DOWNLOAD_FAIL: User '%s' (ID %d) requested Node ID %d but it does not exist.", username, req->user_id, req->node_id);
        goto send_error;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    char type[10];
    strcpy(type, row[0]);
    char name[256];
    strcpy(name, row[1]);
    mysql_free_result(result);

    char filepath[512];
    if (strcmp(type, "folder") == 0)
    {
        char temp_dir[256];
        sprintf(temp_dir, "temp_export_%d", req->node_id);
        char cmd[1024];
        sprintf(cmd, "rm -rf %s && mkdir -p \"%s/%s\"", temp_dir, temp_dir, name);
        system(cmd);

        char base_path[512];
        sprintf(base_path, "%s/%s", temp_dir, name);
        recursive_copy_for_zip(conn, req->node_id, base_path);

        sprintf(filepath, "temp_%d.zip", req->node_id);
        sprintf(cmd, "cd %s && zip -r -q ../%s .", temp_dir, filepath);
        system(cmd);
        sprintf(cmd, "rm -rf %s", temp_dir);
        system(cmd);
        is_temp_zip = 1;
    }
    else
    {
        sprintf(filepath, "%s/%d", SERVER_ROOT, req->node_id);
    }

    struct stat st;
    if (stat(filepath, &st) == -1)
    {
        strcpy(res.message, "File missing on server");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("DOWNLOAD_ERR: User '%s' (ID %d) requested '%s' (ID %d) but physical file missing at '%s'.",
                  username, req->user_id, name, req->node_id, filepath);
        goto send_error;
    }

    res.status = CMD_SUCCESS;
    {
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("DOWNLOAD_SUCCESS: User '%s' (ID %d) downloaded '%s' (ID %d).", username, req->user_id, name, req->node_id);
    }

    res.data_val = st.st_size;
    send_packet(sock, (char *)&res, sizeof(res));
    FILE *fp = fopen(filepath, "rb");
    if (fp)
    {
        char buffer[4096];
        size_t n;
        while ((n = fread(buffer, 1, 4096, fp)) > 0)
            send_all(sock, buffer, n);
        fclose(fp);
    }
    if (is_temp_zip)
        unlink(filepath);
    return;

send_error:
    res.status = CMD_ERROR;
    if (strlen(res.message) == 0)
        strcpy(res.message, "Download Error");
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_rename(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    if (!has_write_access(conn, req->node_id, req->user_id) ||
        !has_write_access(conn, req->parent_id, req->user_id))
    {
        res.status = CMD_ERROR;
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        char node_name[256];
        get_node_name_by_id(conn, req->node_id, node_name);
        write_log("RENAME_FAIL: Access denied. User '%s' (ID %d) cannot rename '%s' (ID %d).", username, req->user_id, node_name, req->node_id);
        strcpy(res.message, "Access denied");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char old_name[256];
    char parent_name[256];
    int parent_id = 0;
    // Lấy tên cũ và parent_id
    {
        char q[512];
        sprintf(q, "SELECT name, parent_id FROM nodes WHERE id=%d", req->node_id);
        if (mysql_query(conn, q) == 0)
        {
            MYSQL_RES *r = mysql_store_result(conn);
            if (r && mysql_num_rows(r) > 0)
            {
                MYSQL_ROW row = mysql_fetch_row(r);
                strcpy(old_name, row[0]);
                parent_id = (row[1]) ? atoi(row[1]) : 0;
            }
            if (r)
                mysql_free_result(r);
        }
        else
        {
            get_node_name_by_id(conn, req->node_id, old_name);
        }
    }
    get_node_name_by_id(conn, parent_id, parent_name);

    char query[512];
    sprintf(query, "UPDATE nodes SET name = '%s' WHERE id = %d", req->arg1, req->node_id);

    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("RENAME_ERR: User '%s' (ID %d) failed to rename '%s' (ID %d) in Parent '%s' (ID %d). DB Error: %s",
                  username, req->user_id, old_name, req->node_id, parent_name, parent_id, mysql_error(conn));
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Rename OK");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("RENAME_SUCCESS: User '%s' (ID %d) renamed '%s' (ID %d) to '%s' in Parent '%s' (ID %d).",
                  username, req->user_id, old_name, req->node_id, req->arg1, parent_name, parent_id);
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_delete(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!check_ownership(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        char node_name[256];
        get_node_name_by_id(conn, req->node_id, node_name);
        write_log("DELETE_FAIL: Access denied. User '%s' (ID %d) cannot delete '%s' (ID %d).", username, req->user_id, node_name, req->node_id);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    // if (!has_write_access(conn, req->node_id, req->user_id) ||
    //     !has_write_access(conn, req->parent_id, req->user_id))
    // {
    //     char username[50];
    //     char node_name[100];
    //     char parent_name[100];

    //     get_username_by_id(conn, req->user_id, username);
    //     get_node_name_by_id(conn, req->node_id, node_name);
    //     get_node_name_by_id(conn, req->parent_id, parent_name);

    //     write_log(
    //         "DELETE_DENY: User '%s' (ID %d) lacks write permission on Node '%s' (ID %d) or Parent '%s' (ID %d).",
    //         username,
    //         req->user_id,
    //         node_name,
    //         req->node_id,
    //         parent_name,
    //         req->parent_id);

    //     res.status = CMD_ERROR;
    //     strcpy(res.message, "Access denied");
    //     send_packet(sock, (char *)&res, sizeof(res));
    //     return;
    // }

    char name[256];
    int parent_id = 0;
    char parent_name[256];
    {
        char q[512];
        sprintf(q, "SELECT name, parent_id FROM nodes WHERE id=%d", req->node_id);
        if (mysql_query(conn, q) == 0)
        {
            MYSQL_RES *r = mysql_store_result(conn);
            if (r && mysql_num_rows(r) > 0)
            {
                MYSQL_ROW row = mysql_fetch_row(r);
                strcpy(name, row[0]);
                parent_id = (row[1]) ? atoi(row[1]) : 0;
            }
            else
            {
                get_node_name_by_id(conn, req->node_id, name);
            }
            if (r)
                mysql_free_result(r);
        }
        else
        {
            get_node_name_by_id(conn, req->node_id, name);
        }
    }
    get_node_name_by_id(conn, parent_id, parent_name);
    delete_physical_file(req->node_id);

    char query[512];
    sprintf(query, "DELETE FROM nodes WHERE id = %d", req->node_id);
    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("DELETE_ERR: User '%s' (ID %d) failed to delete '%s' (ID %d) in Parent '%s' (ID %d). DB Error: %s",
                  username, req->user_id, name, req->node_id, parent_name, parent_id, mysql_error(conn));
    }
    else
    {
        res.status = CMD_SUCCESS;
        strcpy(res.message, "Delete OK");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("DELETE_SUCCESS: User '%s' (ID %d) deleted '%s' (ID %d) from Parent '%s' (ID %d).",
                  username, req->user_id, name, req->node_id, parent_name, parent_id);
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// [FIX] Log hiển thị tên Parent thay vì ID
void handle_copy(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!has_write_access(conn, req->node_id, req->user_id) ||
        !has_write_access(conn, req->parent_id, req->user_id))
    {
        char username[50];
        char node_name[100];
        char parent_name[100];

        get_username_by_id(conn, req->user_id, username);
        get_node_name_by_id(conn, req->node_id, node_name);
        get_node_name_by_id(conn, req->parent_id, parent_name);

        write_log(
            "COPY_DENY: User '%s' (ID %d) lacks write permission on Source '%s' (ID %d) or Destination '%s' (ID %d).",
            username,
            req->user_id,
            node_name,
            req->node_id,
            parent_name,
            req->parent_id);

        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char query[512];
    sprintf(query, "SELECT name, type, size FROM nodes WHERE id=%d", req->node_id);
    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("COPY_ERR: User '%s' (ID %d) failed to fetch source Node ID %d. DB Error: %s", username, req->user_id, req->node_id, mysql_error(conn));
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    MYSQL_RES *r = mysql_store_result(conn);
    if (!r || mysql_num_rows(r) == 0)
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Source not found");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("COPY_FAIL: (1) User '%s' (ID %d) attempted to copy missing source Node ID %d.", username, req->user_id, req->node_id);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    MYSQL_ROW row = mysql_fetch_row(r);
    char src_name[256];
    strcpy(src_name, row[0]);
    char src_type[20];
    strcpy(src_type, row[1]);
    long long src_size = atoll(row[2]);
    mysql_free_result(r);

    char final_name[256];
    int is_folder = (strcmp(src_type, "folder") == 0);
    get_unique_name(conn, req->parent_id, src_name, final_name, is_folder);

    char parent_id_str[20];
    if (req->parent_id <= 0)
    {
        strcpy(parent_id_str, "NULL");
    }
    else
    {
        sprintf(parent_id_str, "%d", req->parent_id);
    }

    // Lưu ý: Đổi %d của parent_id thành %s
    sprintf(query, "INSERT INTO nodes (name, type, size, owner_id, parent_id, created_at) VALUES ('%s', '%s', %lld, %d, %s, NOW())",
            final_name, src_type, src_size, req->user_id, parent_id_str);

    if (mysql_query(conn, query) == 0)
    {
        int new_id = mysql_insert_id(conn);
        if (is_folder)
            copy_recursive_inner(conn, req->node_id, new_id, req->user_id);
        else
            copy_physical_file(req->node_id, new_id);

        res.status = CMD_SUCCESS;
        if (strcmp(src_name, final_name) != 0)
            snprintf(res.message, sizeof(res.message), "Copied as '%.200s'", final_name);
        else
            strcpy(res.message, "Copy success");

        char username[50];
        get_username_by_id(conn, req->user_id, username);
        char parent_name[256];
        get_node_name_by_id(conn, req->parent_id, parent_name);

        write_log("COPY_SUCCESS: User '%s' (ID %d) copied '%s' (ID %d) to Parent '%s' (ID %d) as '%s' (ID %d).",
                  username, req->user_id, src_name, req->node_id, parent_name, req->parent_id, final_name, new_id);
    }
    else
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Copy Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("COPY_ERR: (2) User '%s' (ID %d) failed to insert copy of Node ID %d to Parent ID %d. DB Error: %s",
                  username, req->user_id, req->node_id, req->parent_id, mysql_error(conn));
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

// [FIX] Log hiển thị tên Parent thay vì ID
void handle_move(int sock, MYSQL *conn, RequestPacket *req)
{

    ResponsePacket res;
    memset(&res, 0, sizeof(res));

    if (!has_write_access(conn, req->node_id, req->user_id) ||
        !has_write_access(conn, req->parent_id, req->user_id))
    {
        char username[50];
        char node_name[100];
        char parent_name[100];

        get_username_by_id(conn, req->user_id, username);
        get_node_name_by_id(conn, req->node_id, node_name);
        get_node_name_by_id(conn, req->parent_id, parent_name);

        write_log(
            "MOVE_DENY: User '%s' (ID %d) lacks write permission on Source '%s' (ID %d) or Destination '%s' (ID %d).",
            username,
            req->user_id,
            node_name,
            req->node_id,
            parent_name,
            req->parent_id);

        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char current_name[256];
    char type[20];
    char query[512];
    sprintf(query, "SELECT name, type FROM nodes WHERE id=%d", req->node_id);
    if (mysql_query(conn, query))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("MOVE_ERR: User '%s' (ID %d) failed to fetch Node ID %d. DB Error: %s", username, req->user_id, req->node_id, mysql_error(conn));
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    MYSQL_RES *r = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(r);
    strcpy(current_name, row[0]);
    strcpy(type, row[1]);
    mysql_free_result(r);

    char new_name[256];
    get_unique_name(conn, req->parent_id, current_name, new_name, (strcmp(type, "folder") == 0));

    char parent_id_str[20];
    if (req->parent_id <= 0)
    {
        strcpy(parent_id_str, "NULL");
    }
    else
    {
        sprintf(parent_id_str, "%d", req->parent_id);
    }

    // Lưu ý: Đổi %d của parent_id thành %s
    sprintf(query, "UPDATE nodes SET parent_id=%s, name='%s' WHERE id=%d ",
            parent_id_str, new_name, req->node_id);

    if (mysql_query(conn, query) == 0)
    {
        if (mysql_affected_rows(conn) > 0)
        {
            res.status = CMD_SUCCESS;
            if (strcmp(current_name, new_name) != 0)
                snprintf(res.message, sizeof(res.message), "Moved and renamed to '%.200s'", new_name);
            else
                strcpy(res.message, "Move OK");

            char username[50];
            get_username_by_id(conn, req->user_id, username);
            char parent_name[256];
            get_node_name_by_id(conn, req->parent_id, parent_name); // Lấy tên cha

            write_log("MOVE_SUCCESS: User '%s' (ID %d) moved '%s' (ID %d) to Parent '%s' (ID %d) (New Name: %s).",
                      username, req->user_id, current_name, req->node_id, parent_name, req->parent_id, new_name);
        }
        else
        {
            res.status = CMD_ERROR;
            strcpy(res.message, "Move Failed");
            char username[50];
            get_username_by_id(conn, req->user_id, username);
            write_log("MOVE_FAIL: User '%s' (ID %d) attempted move on Node ID %d but affected 0 rows.", username, req->user_id, req->node_id);
        }
    }
    else
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "DB Error");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("MOVE_ERR: User '%s' (ID %d) failed to move Node ID %d to Parent ID %d. DB Error: %s",
                  username, req->user_id, req->node_id, req->parent_id, mysql_error(conn));
    }
    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_share(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    int has_right = check_has_share_permission(conn, req->node_id, req->user_id);

    if (!has_right)
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        char node_name[256];
        get_node_name_by_id(conn, req->node_id, node_name);
        write_log("SHARE_FAIL: Access denied. User '%s' (ID %d) cannot share '%s' (ID %d).", username, req->user_id, node_name, req->node_id);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char query[512];
    sprintf(query, "SELECT id FROM users WHERE username = '%s'", req->arg1);
    mysql_query(conn, query);
    MYSQL_RES *result = mysql_store_result(conn);
    if (!result || mysql_num_rows(result) == 0)
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "User not found");
        char username[50];
        get_username_by_id(conn, req->user_id, username);
        write_log("SHARE_FAIL: User '%s' (ID %d) attempted to share Node ID %d with missing user '%s'.", username, req->user_id, req->node_id, req->arg1);
        if (result)
            mysql_free_result(result);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    MYSQL_ROW row = mysql_fetch_row(result);
    int target_id = atoi(row[0]);
    mysql_free_result(result);

    share_recursive(conn, req->node_id, target_id, req->arg2);
    res.status = CMD_SUCCESS;
    strcpy(res.message, "Share OK");

    char node_name[256];
    get_node_name_by_id(conn, req->node_id, node_name);
    char username[50];
    get_username_by_id(conn, req->user_id, username);
    write_log("SHARE_SUCCESS: User '%s' (ID %d) shared '%s' (ID %d) with '%s' (Perm: %s).", username, req->user_id, node_name, req->node_id, req->arg1, req->arg2);

    send_packet(sock, (char *)&res, sizeof(res));
}

void handle_get_share_list(int sock, MYSQL *conn, RequestPacket *req)
{
    char query[1024];
    int is_owner = check_ownership(conn, req->node_id, req->user_id);
    if (is_owner)
        sprintf(query, "SELECT u.username, sn.permission FROM shared_nodes sn JOIN users u ON sn.shared_with_user = u.id WHERE sn.node_id = %d", req->node_id);
    else
        sprintf(query, "SELECT u.username, sn.permission FROM shared_nodes sn JOIN users u ON sn.shared_with_user = u.id WHERE sn.node_id = %d AND sn.shared_with_user = %d", req->node_id, req->user_id);

    if (mysql_query(conn, query))
    {
        write_log("GET_SHARE_LIST_ERR: DB Error for Node ID %d.", req->node_id);
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
        ShareEntry se;
        memset(&se, 0, sizeof(se));
        strcpy(se.username, row[0]);
        strcpy(se.permission, row[1]);
        send_packet(sock, (char *)&se, sizeof(se));
    }
    mysql_free_result(result);
}

void handle_remove_share(int sock, MYSQL *conn, RequestPacket *req)
{
    ResponsePacket res;
    memset(&res, 0, sizeof(res));
    char username[50];
    get_username_by_id(conn, req->user_id, username);
    char node_name[256];
    get_node_name_by_id(conn, req->node_id, node_name);

    if (!check_ownership(conn, req->node_id, req->user_id))
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "Access denied");
        write_log("UNSHARE_FAIL: Access denied. User '%s' (ID %d) cannot unshare '%s' (ID %d).", username, req->user_id, node_name, req->node_id);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }

    char query[512];
    sprintf(query, "SELECT id FROM users WHERE username='%s'", req->arg1);
    mysql_query(conn, query);
    MYSQL_RES *r = mysql_store_result(conn);
    if (!r || mysql_num_rows(r) == 0)
    {
        res.status = CMD_ERROR;
        strcpy(res.message, "User not found");
        write_log("UNSHARE_FAIL: User '%s' (ID %d) attempted to unshare '%s' (ID %d) from missing user '%s'.",
                  username, req->user_id, node_name, req->node_id, req->arg1);
        if (r)
            mysql_free_result(r);
        send_packet(sock, (char *)&res, sizeof(res));
        return;
    }
    int target_id = atoi(mysql_fetch_row(r)[0]);
    mysql_free_result(r);

    remove_share_recursive(conn, req->node_id, target_id);
    res.status = CMD_SUCCESS;
    strcpy(res.message, "Unshared");

    write_log("UNSHARE_SUCCESS: User '%s' (ID %d) removed share of '%s' (ID %d) from '%s'.",
              username, req->user_id, node_name, req->node_id, req->arg1);

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

    int current_user_id = -1;

    while (1)
    {
        char *data = recv_packet(sock, &len);
        if (!data)
            break;
        if ((size_t)len < sizeof(RequestPacket))
        {
            free(data);
            continue;
        }
        memcpy(&req, data, sizeof(RequestPacket));
        free(data);

        if (req.user_id > 0)
            current_user_id = req.user_id;

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
        case CMD_GET_PATH:
            handle_get_path(sock, conn, &req);
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
        case CMD_GET_SHARE_LIST:
            handle_get_share_list(sock, conn, &req);
            break;
        case CMD_REMOVE_SHARE:
            handle_remove_share(sock, conn, &req);
            break;
        }
    }

    if (current_user_id > 0)
    {
        printf("[SERVER] Client (User ID: %d) đã ngắt kết nối.\n", current_user_id);
        write_log("DISCONNECT: User ID %d disconnected.", current_user_id);
    }

    mysql_close(conn);
    close_socket(sock);
    return NULL;
}

int main()
{
    signal(SIGINT, handle_termination_signal);
    signal(SIGTERM, handle_termination_signal);

    write_log("=== SERVER STARTED ===");
    printf("=== SERVER STARTED ===\n");
    init_network();
#ifdef _WIN32
    _mkdir(SERVER_ROOT);
#else
    mkdir(SERVER_ROOT, 0777);
#endif
    server_socket_fd = create_server_socket("0.0.0.0", 65432);
    if (server_socket_fd < 0)
        return 1;
    write_log("SERVER_LISTEN: Server running on port 65432");
    printf("SERVER_LISTEN: Server running on port 65432\n");
    while (!stop_server)
    {
        char ip[50];
        int client_sock = accept_client(server_socket_fd, ip);
        if (client_sock < 0)
        {
            if (stop_server)
                break;
            else
                continue;
        }
        if (client_sock >= 0)
        {
            printf("[SERVER] New connection from: %s\n", ip);
            write_log("CONNECT: New connection from %s", ip);
            int *new_sock = malloc(sizeof(int));
            *new_sock = client_sock;
            pthread_t tid;
            pthread_create(&tid, NULL, client_thread, new_sock);
            pthread_detach(tid);
        }
    }
    if (server_socket_fd >= 0)
        close(server_socket_fd);
    write_log("=== SERVER ENDED ===");
    return 0;
}