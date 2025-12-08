#include "server.h"

void *client_handler(void *arg);

MYSQL *conn;
pthread_mutex_t db_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

int main()
{
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║    File Sharing Application - Server     ║\n");
    printf("║           Version 1.0.0                   ║\n");
    printf("╚═══════════════════════════════════════════╝\n\n");

    /* Connect to database */
    printf("[*] Connecting to database...\n");
    if (!db_connect())
    {
        fprintf(stderr, "[-] Database connection failed. Exiting.\n");
        return 1;
    }

    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    /* Create server socket */
    printf("[*] Creating server socket...\n");
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0)
    {
        perror("[-] Socket creation failed");
        db_close();
        exit(EXIT_FAILURE);
    }

    /* Allow reusing address */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("[-] setsockopt failed");
        db_close();
        exit(EXIT_FAILURE);
    }

    /* Bind socket to port */
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("[-] Bind failed");
        db_close();
        exit(EXIT_FAILURE);
    }

    /* Listen for connections */
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("[-] Listen failed");
        db_close();
        exit(EXIT_FAILURE);
    }

    printf("[+] Server listening on port %d\n", PORT);
    printf("[+] Waiting for connections...\n\n");

    /* Accept incoming connections */
    while (1)
    {
        new_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (new_socket < 0)
        {
            perror("[-] Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("[+] New connection from %s:%d\n", client_ip, ntohs(address.sin_port));

        Client *client = malloc(sizeof(Client));
        if (!client)
        {
            fprintf(stderr, "[-] Memory allocation failed\n");
            close(new_socket);
            continue;
        }

        client->socket = new_socket;
        client->logged_in = 0;
        client->user_id = -1;
        client->login_time = 0;
        memset(client->username, 0, MAX_USERNAME_LEN);
        memset(client->session_token, 0, 256);
        memset(client->current_dir, 0, MAX_PATH_LEN);
        strcpy(client->current_dir, "/");
        memcpy(&client->address, &address, sizeof(address));
        client->address_len = addrlen;

        pthread_t tid;
        if (pthread_create(&tid, NULL, client_handler, (void *)client) < 0)
        {
            fprintf(stderr, "[-] Failed to create thread for client\n");
            close(new_socket);
            free(client);
            continue;
        }

        pthread_detach(tid);
    }

    /* Cleanup */
    db_close();
    close(server_fd);
    return 0;
}
