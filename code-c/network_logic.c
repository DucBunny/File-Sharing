#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#else
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#endif

// Khởi tạo Winsock nếu chạy trên Windows
void init_network()
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

// Dọn dẹp Winsock
void cleanup_network()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

// Tạo socket server
socket_t create_server_socket(const char *ip, int port)
{
    socket_t server_fd;
    struct sockaddr_in address;
    int opt = 1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET)
        return INVALID_SOCKET;

#ifndef _WIN32
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

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

// Tạo socket client và kết nối
socket_t connect_to_server(const char *ip, int port)
{
    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET)
        return INVALID_SOCKET;

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        return INVALID_SOCKET;
    }
    return sock;
}

// Gửi chính xác N bytes (Xử lý TCP fragmentation)
int send_all(socket_t sock, const char *data, int len)
{
    int total = 0;
    int bytesleft = len;
    int n;

    while (total < len)
    {
        n = send(sock, data + total, bytesleft, 0);
        if (n == -1)
        {
            break;
        }
        total += n;
        bytesleft -= n;
    }
    return (n == -1) ? -1 : total;
}

// Nhận chính xác N bytes
int recv_all(socket_t sock, char *buffer, int len)
{
    int total = 0;
    int bytesleft = len;
    int n;

    while (total < len)
    {
        n = recv(sock, buffer + total, bytesleft, 0);
        if (n <= 0)
        {
            return -1;
        } // Error or connection closed
        total += n;
        bytesleft -= n;
    }
    return total;
}

// Gửi gói tin có kèm độ dài (4 bytes length header + data)
int send_packet(socket_t sock, const char *data, int len)
{
    // Chuyển độ dài về Big Endian để đồng bộ mạng
    unsigned int net_len = htonl(len);
    if (send_all(sock, (char *)&net_len, 4) == -1)
        return -1;
    return send_all(sock, data, len);
}

// Nhận gói tin có kèm độ dài
// Trả về con trỏ tới dữ liệu (Python phải copy hoặc free)
// out_len sẽ chứa độ dài dữ liệu
char *recv_packet(socket_t sock, int *out_len)
{
    unsigned int net_len = 0;

    // 1. Đọc header độ dài (4 bytes)
    if (recv_all(sock, (char *)&net_len, 4) == -1)
    {
        *out_len = -1;
        return NULL;
    }

    int len = ntohl(net_len);
    *out_len = len;

    // 2. Cấp phát bộ nhớ để đọc dữ liệu
    char *buffer = (char *)malloc(len + 1); // +1 cho null terminator an toàn
    if (buffer == NULL)
        return NULL;

    // 3. Đọc dữ liệu thực
    if (recv_all(sock, buffer, len) == -1)
    {
        free(buffer);
        *out_len = -1;
        return NULL;
    }

    buffer[len] = '\0'; // Null terminate phòng trường hợp in chuỗi
    return buffer;      // Trả về con trỏ cho Python
}

// Hàm để Python giải phóng bộ nhớ do C cấp phát
void free_mem(char *ptr)
{
    if (ptr != NULL)
        free(ptr);
}

// Đóng socket
void close_socket(socket_t sock)
{
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}