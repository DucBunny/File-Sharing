#ifndef SERVER_PERMISSIONS_H
#define SERVER_PERMISSIONS_H

#include <mysql/mysql.h>

// Permission checking functions
int check_has_share_permission(MYSQL *conn, int node_id, int user_id);
int check_access_recursive(MYSQL *conn, int node_id, int user_id);
int has_write_access(MYSQL *conn, int node_id, int user_id);
int has_read_access(MYSQL *conn, int node_id, int user_id);

// Share operations
void share_recursive(MYSQL *conn, int node_id, int target_user_id, const char *permission);
void remove_share_recursive(MYSQL *conn, int node_id, int target_user_id);

#endif // SERVER_PERMISSIONS_H
