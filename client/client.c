#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>

#define PORT 9000
#define SERVER_IP "127.0.0.1"
#define BUFFER_SIZE 8192
#define FILE_CHUNK_SIZE 65536
#define MAX_CMD_LEN 256

typedef struct
{
    int socket;
    char username[64];
    char session_token[256];
    int logged_in;
} ClientSession;

ClientSession session;

/* Function prototypes */
void print_menu();
void connect_to_server();
void disconnect_from_server();
int send_command(const char *cmd, const char *arg1, const char *arg2, const char *arg3, char *response);
void cmd_register();
void cmd_login();
void cmd_logout();
void cmd_list_dir();
void cmd_upload_file();
void cmd_download_file();
void cmd_delete_file();
void cmd_search_file();
void cmd_rename_file();
void cmd_create_dir();
void parse_response(const char *response, int *status, char *message, char *data);

int main()
{
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║    File Sharing Application - Client    ║\n");
    printf("║           Version 1.0.0                   ║\n");
    printf("╚═══════════════════════════════════════════╝\n\n");

    memset(&session, 0, sizeof(session));
    session.socket = -1;

    connect_to_server();

    char choice[256];
    while (1)
    {
        print_menu();
        printf("Enter choice: ");
        fgets(choice, sizeof(choice), stdin);
        choice[strcspn(choice, "\n")] = 0;

        switch (choice[0])
        {
        case '1':
            cmd_register();
            break;
        case '2':
            cmd_login();
            break;
        case '3':
            cmd_logout();
            break;
        case '4':
            cmd_list_dir();
            break;
        case '5':
            cmd_upload_file();
            break;
        case '6':
            cmd_download_file();
            break;
        case '7':
            cmd_delete_file();
            break;
        case '8':
            cmd_search_file();
            break;
        case '9':
            cmd_rename_file();
            break;
        case '10':
            cmd_create_dir();
            break;
        case '0':
            if (session.logged_in)
            {
                char response[BUFFER_SIZE];
                send_command("LOGOUT", "", "", "", response);
            }
            printf("Exiting...\n");
            disconnect_from_server();
            return 0;
        default:
            printf("Invalid choice!\n");
        }
    }

    return 0;
}

void print_menu()
{
    printf("\n╔═══════════════════════════════════════════╗\n");
    if (session.logged_in)
    {
        printf("║  Logged in as: %-26s ║\n", session.username);
    }
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║ 1. Register                             ║\n");
    printf("║ 2. Login                                ║\n");
    printf("║ 3. Logout                               ║\n");
    printf("║ 4. List Directory                       ║\n");
    printf("║ 5. Upload File                          ║\n");
    printf("║ 6. Download File                        ║\n");
    printf("║ 7. Delete File                          ║\n");
    printf("║ 8. Search File                          ║\n");
    printf("║ 9. Rename File                          ║\n");
    printf("║ 10. Create Directory                    ║\n");
    printf("║ 0. Exit                                 ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
}

void connect_to_server()
{
    session.socket = socket(AF_INET, SOCK_STREAM, 0);
    if (session.socket < 0)
    {
        perror("[-] Socket creation failed");
        exit(1);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(session.socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("[-] Connection failed");
        exit(1);
    }

    printf("[+] Connected to server at %s:%d\n", SERVER_IP, PORT);
}

void disconnect_from_server()
{
    if (session.socket >= 0)
    {
        close(session.socket);
        printf("[+] Disconnected from server\n");
    }
}

int send_command(const char *cmd, const char *arg1, const char *arg2, const char *arg3, char *response)
{
    if (session.socket < 0)
    {
        printf("[-] Not connected to server\n");
        return 0;
    }

    char request[BUFFER_SIZE];
    snprintf(request, sizeof(request), "%s|%s|%s|%s", cmd, arg1 ? arg1 : "", arg2 ? arg2 : "", arg3 ? arg3 : "");

    if (send(session.socket, request, strlen(request), 0) < 0)
    {
        perror("[-] Send failed");
        return 0;
    }

    memset(response, 0, BUFFER_SIZE);
    int bytes = recv(session.socket, response, BUFFER_SIZE - 1, 0);
    if (bytes <= 0)
    {
        printf("[-] Receive failed\n");
        return 0;
    }

    response[bytes] = '\0';
    return 1;
}

void parse_response(const char *response, int *status, char *message, char *data)
{
    sscanf(response, "%d|%[^|]|%[^\n]", status, message, data);
}

void cmd_register()
{
    char username[64];
    char password[64];
    char email[128];

    printf("\n--- Register ---\n");
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;

    printf("Email: ");
    fgets(email, sizeof(email), stdin);
    email[strcspn(email, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("REGISTER", username, password, email, response))
    {
        int status;
        char message[256] = {0};
        parse_response(response, &status, message, "");
        if (status == 0)
        {
            printf("[+] %s\n", message);
        }
        else
        {
            printf("[-] %s\n", message);
        }
    }
}

void cmd_login()
{
    char username[64];
    char password[64];

    printf("\n--- Login ---\n");
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = 0;

    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("LOGIN", username, password, "", response))
    {
        int status;
        char message[256] = {0};
        char token[256] = {0};
        sscanf(response, "%d|%[^|]|%s", &status, message, token);

        if (status == 0)
        {
            session.logged_in = 1;
            strcpy(session.username, username);
            strcpy(session.session_token, token);
            printf("[+] %s\n", message);
        }
        else
        {
            printf("[-] %s\n", message);
        }
    }
}

void cmd_logout()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char response[BUFFER_SIZE];
    if (send_command("LOGOUT", "", "", "", response))
    {
        int status;
        char message[256] = {0};
        parse_response(response, &status, message, "");

        if (status == 0)
        {
            session.logged_in = 0;
            memset(session.username, 0, sizeof(session.username));
            memset(session.session_token, 0, sizeof(session.session_token));
            printf("[+] %s\n", message);
        }
        else
        {
            printf("[-] %s\n", message);
        }
    }
}

void cmd_list_dir()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char path[512];
    printf("\nDirectory path (default '/'): ");
    fgets(path, sizeof(path), stdin);
    path[strcspn(path, "\n")] = 0;
    if (strlen(path) == 0)
        strcpy(path, "/");

    char response[BUFFER_SIZE];
    if (send_command("LIST", path, "", "", response))
    {
        printf("\n%s\n", response);
    }
}

void cmd_upload_file()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char filename[256];
    char dest_path[512];

    printf("\nLocal file path: ");
    fgets(filename, sizeof(filename), stdin);
    filename[strcspn(filename, "\n")] = 0;

    printf("Destination path on server: ");
    fgets(dest_path, sizeof(dest_path), stdin);
    dest_path[strcspn(dest_path, "\n")] = 0;

    FILE *file = fopen(filename, "rb");
    if (!file)
    {
        printf("[-] Cannot open file\n");
        return;
    }

    fseek(file, 0, SEEK_END);
    long long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    printf("[*] Uploading file (%lld bytes)...\n", file_size);

    char response[BUFFER_SIZE];
    char size_str[32];
    snprintf(size_str, sizeof(size_str), "%lld", file_size);

    if (send_command("UPLOAD_START", dest_path, size_str, "", response))
    {
        printf("[+] Upload started\n");

        char buffer[4096];
        long long total_sent = 0;

        while (total_sent < file_size)
        {
            size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
            if (bytes_read <= 0)
                break;

            send(session.socket, buffer, bytes_read, 0);
            total_sent += bytes_read;
        }

        printf("[+] File data sent (%lld bytes)\n", total_sent);
    }

    fclose(file);
    printf("[+] Upload complete\n");
}

void cmd_download_file()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char file_path[512];
    char local_path[256];

    printf("\nFile path on server: ");
    fgets(file_path, sizeof(file_path), stdin);
    file_path[strcspn(file_path, "\n")] = 0;

    printf("Local save path: ");
    fgets(local_path, sizeof(local_path), stdin);
    local_path[strcspn(local_path, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("DOWNLOAD_START", file_path, "", "", response))
    {
        printf("[+] Download started\n");
    }

    printf("[+] Download complete\n");
}

void cmd_delete_file()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char file_path[512];
    printf("\nFile path to delete: ");
    fgets(file_path, sizeof(file_path), stdin);
    file_path[strcspn(file_path, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("DELETE", file_path, "", "", response))
    {
        int status;
        char message[256] = {0};
        parse_response(response, &status, message, "");
        printf("[*] %s\n", message);
    }
}

void cmd_search_file()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char pattern[256];
    printf("\nSearch pattern: ");
    fgets(pattern, sizeof(pattern), stdin);
    pattern[strcspn(pattern, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("SEARCH", pattern, "", "", response))
    {
        printf("\n%s\n", response);
    }
}

void cmd_rename_file()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char old_path[512];
    char new_path[512];

    printf("\nOld file path: ");
    fgets(old_path, sizeof(old_path), stdin);
    old_path[strcspn(old_path, "\n")] = 0;

    printf("New file path: ");
    fgets(new_path, sizeof(new_path), stdin);
    new_path[strcspn(new_path, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("RENAME", old_path, new_path, "", response))
    {
        int status;
        char message[256] = {0};
        parse_response(response, &status, message, "");
        printf("[*] %s\n", message);
    }
}

void cmd_create_dir()
{
    if (!session.logged_in)
    {
        printf("[-] Not logged in\n");
        return;
    }

    char dir_path[512];
    printf("\nDirectory path: ");
    fgets(dir_path, sizeof(dir_path), stdin);
    dir_path[strcspn(dir_path, "\n")] = 0;

    char response[BUFFER_SIZE];
    if (send_command("MKDIR", dir_path, "", "", response))
    {
        int status;
        char message[256] = {0};
        parse_response(response, &status, message, "");
        printf("[*] %s\n", message);
    }
}
