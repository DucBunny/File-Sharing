#ifndef SERVER_HANDLERS_H
#define SERVER_HANDLERS_H

#include <mysql/mysql.h>
#include "../common/protocol.h"

// Request handlers
void handle_login(int sock, MYSQL *conn, RequestPacket *req);
void handle_register(int sock, MYSQL *conn, RequestPacket *req);
void handle_create_folder(int sock, MYSQL *conn, RequestPacket *req);
void handle_list(int sock, MYSQL *conn, RequestPacket *req);
void handle_search(int sock, MYSQL *conn, RequestPacket *req);
void handle_get_path(int sock, MYSQL *conn, RequestPacket *req);
void handle_upload(int sock, MYSQL *conn, RequestPacket *req);
void handle_download(int sock, MYSQL *conn, RequestPacket *req);
void handle_rename(int sock, MYSQL *conn, RequestPacket *req);
void handle_delete(int sock, MYSQL *conn, RequestPacket *req);
void handle_copy(int sock, MYSQL *conn, RequestPacket *req);
void handle_move(int sock, MYSQL *conn, RequestPacket *req);
void handle_share(int sock, MYSQL *conn, RequestPacket *req);
void handle_get_share_list(int sock, MYSQL *conn, RequestPacket *req);
void handle_remove_share(int sock, MYSQL *conn, RequestPacket *req);

#endif // SERVER_HANDLERS_H
