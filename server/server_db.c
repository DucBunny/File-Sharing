#include "server_db.h"
#include "server_logging.h"
#include "../common/config.h"
#include <stdio.h>
#include <string.h>

MYSQL *connect_db(void)
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
