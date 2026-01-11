#include "server_file_ops.h"
#include "../common/config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

void delete_physical_file(long long file_id)
{
    char filepath[512];
    sprintf(filepath, "%s/%lld", SERVER_ROOT, file_id);
    unlink(filepath);
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
