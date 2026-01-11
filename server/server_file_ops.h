#ifndef SERVER_FILE_OPS_H
#define SERVER_FILE_OPS_H

#include <mysql/mysql.h>

// File operations
void delete_physical_file(long long file_id);
int copy_physical_file(int src_id, int dest_id);
void copy_recursive_inner(MYSQL *conn, int src_parent_id, int dest_parent_id, int owner_id);
void recursive_copy_for_zip(MYSQL *conn, int node_id, const char *base_path);

#endif // SERVER_FILE_OPS_H
