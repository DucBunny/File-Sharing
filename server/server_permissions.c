#include "server_permissions.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

        cur = parent;
    }
    return 0;
}

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

        cur = parent;
    }
    return 0;
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
