#include "server.h"

void send_response(int socket, int status_code, const char *message, const char *data);
CommandType parse_command(const char *cmd_str);

void *client_handler(void *arg)
{
    Client *client = (Client *)arg;
    char buffer[BUFFER_SIZE];
    char cmd_str[MAX_CMD_LEN];
    char arg1[MAX_ARG_LEN];
    char arg2[MAX_ARG_LEN];
    char arg3[MAX_ARG_LEN];

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->address.sin_addr, client_ip, INET_ADDRSTRLEN);

    printf("[*] Client thread started from %s\n", client_ip);

    /* Protocol: CMD|ARG1|ARG2|ARG3 */
    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);
        memset(cmd_str, 0, MAX_CMD_LEN);
        memset(arg1, 0, MAX_ARG_LEN);
        memset(arg2, 0, MAX_ARG_LEN);
        memset(arg3, 0, MAX_ARG_LEN);

        int bytes = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0)
        {
            printf("[*] Client %s disconnected\n", client_ip);
            break;
        }

        buffer[bytes] = '\0';

        /* Parse command */
        int parsed = sscanf(buffer, "%31[^|]|%511[^|]|%511[^|]|%511s",
                            cmd_str, arg1, arg2, arg3);

        CommandType cmd = parse_command(cmd_str);

        switch (cmd)
        {
        /* ==================== Authentication ==================== */
        case CMD_REGISTER:
        {
            if (parsed < 3)
            {
                send_response(client->socket, -1, "Invalid arguments", "");
                break;
            }

            if (user_register(arg1, arg2, arg3))
            {
                send_response(client->socket, 0, "Registration successful", "");
            }
            else
            {
                send_response(client->socket, -1, "Registration failed", "");
            }
            break;
        }

        case CMD_LOGIN:
        {
            if (parsed < 2)
            {
                send_response(client->socket, -1, "Invalid arguments", "");
                break;
            }

            char token[256];
            if (user_login(arg1, arg2, token))
            {
                client->logged_in = 1;
                client->login_time = time(NULL);
                strcpy(client->username, arg1);

                /* Get user ID */
                char query[256];
                snprintf(query, sizeof(query), "SELECT user_id FROM users WHERE username='%s'", arg1);
                MYSQL_RES *result = db_query_result(query);
                if (result && mysql_num_rows(result) > 0)
                {
                    MYSQL_ROW row = mysql_fetch_row(result);
                    client->user_id = atoi(row[0]);
                    mysql_free_result(result);
                }

                strcpy(client->session_token, token);
                send_response(client->socket, 0, "Login successful", token);
                log_activity(client->user_id, "LOGIN", "SESSION", "", "", "User logged in", client_ip);
            }
            else
            {
                send_response(client->socket, -1, "Invalid credentials", "");
            }
            break;
        }

        case CMD_LOGOUT:
        {
            if (client->logged_in)
            {
                log_activity(client->user_id, "LOGOUT", "SESSION", "", "", "User logged out", client_ip);
                user_logout(client->session_token);
                client->logged_in = 0;
                send_response(client->socket, 0, "Logout successful", "");
            }
            else
            {
                send_response(client->socket, -1, "Not logged in", "");
            }
            break;
        }

        case CMD_REFRESH_SESSION:
        {
            int user_id;
            if (user_verify_session(client->session_token, &user_id))
            {
                send_response(client->socket, 0, "Session valid", "");
            }
            else
            {
                send_response(client->socket, -1, "Session expired", "");
                client->logged_in = 0;
            }
            break;
        }

        /* ==================== File Operations ==================== */
        case CMD_LIST_DIR:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            char response[BUFFER_SIZE];
            if (list_directory(client->user_id, arg1, response))
            {
                send_response(client->socket, 0, "OK", response);
                log_activity(client->user_id, "LIST", "DIR", "", arg1, "", client_ip);
            }
            else
            {
                send_response(client->socket, -1, "Failed to list directory", "");
            }
            break;
        }

        case CMD_UPLOAD_START:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            long long file_size = atoll(arg2);
            const char *dest_path = arg1; // <-- đường dẫn trên server do client gửi

            // Lấy file name từ dest_path
            const char *file_name = strrchr(dest_path, '/');
            if (!file_name)
                file_name = dest_path;
            else
                file_name++;
            printf("file name từ dest_path: %s\n", file_name);

            FILE *fp = upload_file_start(client->user_id, dest_path, file_name, file_size);
            if (!fp)
            {
                send_response(client->socket, -1, "Upload failed to start", "");
                break;
            }

            send_response(client->socket, 0, "Upload ready", "");

            if (upload_file_data(client->socket, fp, file_size))
            {
                fclose(fp);
                printf("[+] File upload complete: %s\n", dest_path);

                db_mark_file_uploaded(dest_path); // <-- cập nhật DB thành trạng thái DONE
            }
            else
            {
                fclose(fp);
                printf("[-] Upload failed: connection lost\n");
                db_mark_file_failed(dest_path); // <-- cập nhật trạng thái FAILED
            }

            break;
        }

        case CMD_DOWNLOAD_START:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            FileTransfer transfer;
            if (download_file_start(client->user_id, arg1, &transfer))
            {
                char response[256];
                snprintf(response, sizeof(response), "%lld", transfer.total_size);
                send_response(client->socket, 0, "Download ready", response);
                log_activity(client->user_id, "DOWNLOAD", "FILE", "", arg1, "", client_ip);
            }
            else
            {
                send_response(client->socket, -1, "File not found", "");
            }
            break;
        }

        case CMD_DELETE_FILE:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            if (delete_file(client->user_id, arg1))
            {
                send_response(client->socket, 0, "File deleted", "");
            }
            else
            {
                send_response(client->socket, -1, "Failed to delete file", "");
            }
            break;
        }

        case CMD_SEARCH_FILE:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            char response[BUFFER_SIZE];
            if (search_file(client->user_id, arg1, response))
            {
                send_response(client->socket, 0, "Search results", response);
                log_activity(client->user_id, "SEARCH", "FILE", "", arg1, "", client_ip);
            }
            else
            {
                send_response(client->socket, -1, "Search failed", "");
            }
            break;
        }

        case CMD_RENAME_FILE:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            if (rename_file(client->user_id, arg1, arg2))
            {
                send_response(client->socket, 0, "File renamed", "");
            }
            else
            {
                send_response(client->socket, -1, "Failed to rename file", "");
            }
            break;
        }

        case CMD_COPY_FILE:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            if (copy_file(client->user_id, arg1, arg2))
            {
                send_response(client->socket, 0, "File copied", "");
            }
            else
            {
                send_response(client->socket, -1, "Failed to copy file", "");
            }
            break;
        }

        case CMD_MOVE_FILE:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            if (move_file(client->user_id, arg1, arg2))
            {
                send_response(client->socket, 0, "File moved", "");
            }
            else
            {
                send_response(client->socket, -1, "Failed to move file", "");
            }
            break;
        }

        /* ==================== Directory Operations ==================== */
        case CMD_CREATE_DIR:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            int perms = arg2 ? atoi(arg2) : DEFAULT_DIR_PERMS;
            if (create_directory(client->user_id, arg1, perms))
            {
                send_response(client->socket, 0, "Directory created", "");
                log_activity(client->user_id, "CREATE_DIR", "DIR", "", arg1, "", client_ip);
            }
            else
            {
                send_response(client->socket, -1, "Failed to create directory", "");
            }
            break;
        }

        case CMD_DELETE_DIR:
        {
            if (!client->logged_in)
            {
                send_response(client->socket, -1, "Not authenticated", "");
                break;
            }

            if (delete_directory(client->user_id, arg1))
            {
                send_response(client->socket, 0, "Directory deleted", "");
            }
            else
            {
                send_response(client->socket, -1, "Failed to delete directory", "");
            }
            break;
        }

        case CMD_QUIT:
        {
            send_response(client->socket, 0, "Goodbye", "");
            printf("[*] Client %s closed connection\n", client_ip);
            goto cleanup;
        }

        default:
        {
            send_response(client->socket, -1, "Unknown command", "");
            break;
        }
        }
    }

cleanup:
    if (client->logged_in)
    {
        user_logout(client->session_token);
    }
    close(client->socket);
    free(client);
    printf("[*] Client thread terminated\n");
    pthread_exit(NULL);
    return NULL;
}

void send_response(int socket, int status_code, const char *message, const char *data)
{
    char response[BUFFER_SIZE];

    /* Protocol: STATUS|MESSAGE|DATA */
    if (data && strlen(data) > 0)
    {
        snprintf(response, sizeof(response), "%d|%s|%s",
                 status_code, message, data);
    }
    else
    {
        snprintf(response, sizeof(response), "%d|%s",
                 status_code, message);
    }

    send(socket, response, strlen(response), 0);
}

CommandType parse_command(const char *cmd_str)
{
    if (!cmd_str)
        return CMD_UNKNOWN;

    if (strcmp(cmd_str, "REGISTER") == 0)
        return CMD_REGISTER;
    if (strcmp(cmd_str, "LOGIN") == 0)
        return CMD_LOGIN;
    if (strcmp(cmd_str, "LOGOUT") == 0)
        return CMD_LOGOUT;
    if (strcmp(cmd_str, "REFRESH") == 0)
        return CMD_REFRESH_SESSION;
    if (strcmp(cmd_str, "LIST") == 0)
        return CMD_LIST_DIR;
    if (strcmp(cmd_str, "UPLOAD_START") == 0)
        return CMD_UPLOAD_START;
    if (strcmp(cmd_str, "DOWNLOAD_START") == 0)
        return CMD_DOWNLOAD_START;
    if (strcmp(cmd_str, "DELETE") == 0)
        return CMD_DELETE_FILE;
    if (strcmp(cmd_str, "SEARCH") == 0)
        return CMD_SEARCH_FILE;
    if (strcmp(cmd_str, "RENAME") == 0)
        return CMD_RENAME_FILE;
    if (strcmp(cmd_str, "COPY") == 0)
        return CMD_COPY_FILE;
    if (strcmp(cmd_str, "MOVE") == 0)
        return CMD_MOVE_FILE;
    if (strcmp(cmd_str, "MKDIR") == 0)
        return CMD_CREATE_DIR;
    if (strcmp(cmd_str, "RMDIR") == 0)
        return CMD_DELETE_DIR;
    if (strcmp(cmd_str, "QUIT") == 0)
        return CMD_QUIT;

    return CMD_UNKNOWN;
}
