#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

// Tạo socket server
socket_t create_server_socket(const char *ip, int port)
{
    socket_t server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
        return INVALID_SOCKET;

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(ip);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        return INVALID_SOCKET;
    }

    if (listen(server_fd, 5) < 0)
    {
        return INVALID_SOCKET;
    }

    return server_fd;
}

// Chấp nhận kết nối từ Client
socket_t accept_client(socket_t server_fd, char *client_ip_buffer)
{
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    socket_t new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

    if (new_socket != INVALID_SOCKET && client_ip_buffer != NULL)
    {
        strcpy(client_ip_buffer, inet_ntoa(address.sin_addr));
    }
    return new_socket;
}