#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

// Định nghĩa kiểu socket cho Linux
typedef int socket_t;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

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
    close(sock);
}