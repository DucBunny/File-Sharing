#ifndef SERVER_DB_H
#define SERVER_DB_H

#include <mysql/mysql.h>

// Database connection
MYSQL *connect_db(void);

// Utility functions
void get_username_by_id(MYSQL *conn, int user_id, char *out_name);
void get_node_name_by_id(MYSQL *conn, int node_id, char *out_name);
int check_ownership(MYSQL *conn, int node_id, int user_id);
int check_is_shared(MYSQL *conn, int node_id, int user_id);
void get_unique_name(MYSQL *conn, int parent_id, char *original_name, char *out_name, int is_folder);

#endif // SERVER_DB_H
