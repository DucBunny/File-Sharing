#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <mysql/mysql.h>
#include "server_logging.h"
#include "server_db.h"
#include "server_handlers.h"
#include "../common/protocol.h"
#include "../common/network_logic.c"
#include "../common/config.h"

static volatile sig_atomic_t stop_server = 0;
static int server_socket_fd = -1;

void handle_termination_signal(int signum)
{
    (void)signum;
    stop_server = 1;
    const char nl = '\n';
    write(STDOUT_FILENO, &nl, 1);
    if (server_socket_fd >= 0)
    {
        close(server_socket_fd);
        server_socket_fd = -1;
    }
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

int main(void)
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
