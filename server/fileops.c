#include "server.h"
#include <errno.h>

int ensure_parent_dir(const char *path)
{
    char tmp[512];
    strcpy(tmp, path);

    char *p = strrchr(tmp, '/'); // tìm thư mục cha
    if (!p)
        return 1; // không có /, không cần tạo

    *p = 0; // cắt bỏ filename, chỉ còn thư mục
    struct stat st;
    if (stat(tmp, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
            return 1; // đã tồn tại
        else
            return 0; // tồn tại nhưng không phải thư mục
    }

    // tạo thư mục cha
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return 0;

    return 1;
}

int list_directory(int user_id, const char *path, char *response)
{
    if (!path || !response)
        return 0;

    char *abs_path = get_absolute_path(user_id, path);
    if (!abs_path)
    {
        snprintf(response, BUFFER_SIZE, "ERROR|Cannot resolve path");
        return 0;
    }

    /* Check permissions */
    if (!check_permission(user_id, path, 0, PERM_OWNER_READ))
    {
        snprintf(response, BUFFER_SIZE, "ERROR|Permission denied");
        return 0;
    }

    DIR *dir = opendir(abs_path);
    if (!dir)
    {
        snprintf(response, BUFFER_SIZE, "ERROR|Cannot open directory: %s", strerror(errno));
        return 0;
    }

    strcpy(response, "OK|");
    struct dirent *entry;
    struct stat file_stat;
    char buffer[256];
    char perm_str[11];

    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", abs_path, entry->d_name);

        if (stat(full_path, &file_stat) == 0)
        {
            char type = S_ISDIR(file_stat.st_mode) ? 'D' : 'F';

            /* Format: TYPE|NAME|SIZE|OWNER|PERMS */
            snprintf(buffer, sizeof(buffer), "%c|%s|%lld|%d|%o\n",
                     type, entry->d_name, (long long)file_stat.st_size,
                     file_stat.st_uid, file_stat.st_mode & 0777);

            strcat(response, buffer);
        }
    }

    closedir(dir);
    return 1;
}

int upload_file_data(int client_socket, FILE *out, long long file_size)
{
    char buffer[4096];
    long long total = 0;

    while (total < file_size)
    {
        int n = recv(client_socket, buffer, sizeof(buffer), 0);
        if (n <= 0)
            return 0;

        fwrite(buffer, 1, n, out);
        total += n;
    }

    int n = recv(client_socket, buffer, sizeof(buffer), 0);
    printf("[*] Received %d bytes\n", n);

    return 1;
}

FILE *upload_file_start(int user_id, const char *dest_path,
                        const char *filename, long long file_size)
{
    char *abs_path = get_absolute_path(user_id, dest_path);
    if (!abs_path)
    {
        fprintf(stderr, "[-] Cannot resolve path\n");
        return NULL;
    }

    // Check xem file đã tồn tại chưa
    if (path_exists(abs_path))
    {
        fprintf(stderr, "[-] File already exists: %s\n", abs_path);
        return NULL;
    }

    // Tạo thư mục cha nếu cần
    if (!ensure_parent_dir(abs_path))
    {
        fprintf(stderr, "[-] Cannot create parent dir for %s\n", abs_path);
        return NULL;
    }

    // Mở file
    FILE *fp = fopen(abs_path, "wb");
    if (!fp)
    {
        fprintf(stderr, "[-] Cannot open file for writing\n");
        return NULL;
    }

    // Lưu vào database (trạng thái đang upload)
    char safe_filename[512];
    char safe_path[512];
    extern MYSQL *conn;

    mysql_real_escape_string(conn, safe_filename, (char *)filename, strlen(filename));
    mysql_real_escape_string(conn, safe_path, (char *)dest_path, strlen(dest_path));

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO files (owner_id, filename, file_path, file_size, status, permissions) "
             "VALUES (%d, '%s', '%s', %lld, 'UPLOADING', %d)",
             user_id, safe_filename, safe_path, (long long)file_size, DEFAULT_FILE_PERMS);

    if (!db_query(query))
    {
        fprintf(stderr, "[-] Failed to insert DB record\n");
        fclose(fp);
        return NULL;
    }

    printf("[+] Database entry created: %s (%lld bytes)\n", dest_path, file_size);

    return fp;
}

void db_mark_file_uploaded(const char *dest_path)
{
    char safe_path[512];
    extern MYSQL *conn;
    mysql_real_escape_string(conn, safe_path, (char *)dest_path, strlen(dest_path));

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE files SET status='DONE' WHERE file_path='%s'", safe_path);
    db_query(query);
}

void db_mark_file_failed(const char *dest_path)
{
    char safe_path[512];
    extern MYSQL *conn;
    mysql_real_escape_string(conn, safe_path, (char *)dest_path, strlen(dest_path));

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE files SET status='FAILED' WHERE file_path='%s'", safe_path);
    db_query(query);
}

int upload_file_chunk(int user_id, int file_id, const char *data, int chunk_size)
{
    if (!data || chunk_size <= 0 || file_id <= 0)
        return 0;

    /* Get file path from database */
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT file_path FROM files WHERE file_id=%d AND owner_id=%d",
             file_id, user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    char *file_path = row[0];
    char *abs_path = get_absolute_path(user_id, file_path);
    mysql_free_result(result);

    if (!abs_path)
        return 0;

    /* Append chunk to file */
    FILE *file = fopen(abs_path, "ab");
    if (!file)
    {
        perror("[-] Failed to open file for writing");
        return 0;
    }

    size_t written = fwrite(data, 1, chunk_size, file);
    fclose(file);

    if (written != chunk_size)
    {
        fprintf(stderr, "[-] Failed to write complete chunk\n");
        return 0;
    }

    return 1;
}

int upload_file_end(int user_id, int file_id, const char *file_hash)
{
    if (file_id <= 0)
        return 0;

    /* Get file path and compute actual hash */
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT file_path FROM files WHERE file_id=%d AND owner_id=%d",
             file_id, user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    char *file_path = row[0];
    char *abs_path = get_absolute_path(user_id, file_path);
    mysql_free_result(result);

    if (!abs_path)
        return 0;

    /* Compute file hash */
    char *computed_hash = compute_file_hash(abs_path);
    if (!computed_hash)
    {
        fprintf(stderr, "[-] Failed to compute file hash\n");
        return 0;
    }

    /* Verify hash if provided */
    if (file_hash && strcmp(computed_hash, file_hash) != 0)
    {
        fprintf(stderr, "[-] File hash mismatch. File may be corrupted.\n");
        return 0;
    }

    /* Update database with actual file size and hash */
    long long actual_size = get_file_size(abs_path);
    snprintf(query, sizeof(query),
             "UPDATE files SET file_size=%lld, file_hash='%s', updated_at=NOW() WHERE file_id=%d",
             actual_size, computed_hash, file_id);

    if (!db_query(query))
        return 0;

    /* Log activity */
    log_activity(user_id, "UPLOAD", "FILE", "", file_path, "File uploaded successfully", "");

    printf("[+] File upload completed: %s\n", file_path);
    return 1;
}

int download_file_start(int user_id, const char *file_path, FileTransfer *transfer)
{
    if (!file_path || !transfer)
        return 0;

    if (!check_permission(user_id, file_path, 0, PERM_OWNER_READ))
    {
        fprintf(stderr, "[-] Permission denied for file: %s\n", file_path);
        return 0;
    }

    char *abs_path = get_absolute_path(user_id, file_path);
    if (!abs_path)
        return 0;

    if (!path_exists(abs_path))
    {
        fprintf(stderr, "[-] File does not exist: %s\n", abs_path);
        return 0;
    }

    long long file_size = get_file_size(abs_path);
    if (file_size < 0)
        return 0;

    transfer->total_size = file_size;
    transfer->transferred = 0;
    strncpy(transfer->file_path, file_path, MAX_PATH_LEN - 1);

    printf("[+] Download started: %s (size: %lld bytes)\n", file_path, file_size);
    return 1;
}

int download_file_chunk(int user_id, FileTransfer *transfer, char *buffer)
{
    if (!transfer || !buffer)
        return 0;

    char *abs_path = get_absolute_path(user_id, transfer->file_path);
    if (!abs_path)
        return 0;

    FILE *file = fopen(abs_path, "rb");
    if (!file)
    {
        perror("[-] Failed to open file for reading");
        return 0;
    }

    fseek(file, transfer->transferred, SEEK_SET);
    size_t bytes_read = fread(buffer, 1, FILE_CHUNK_SIZE, file);
    fclose(file);

    if (bytes_read > 0)
    {
        transfer->transferred += bytes_read;
        return bytes_read;
    }

    return 0;
}

int download_file_end(int user_id, int file_id)
{
    /* Log activity */
    log_activity(user_id, "DOWNLOAD", "FILE", "", "", "File downloaded successfully", "");
    return 1;
}

int delete_file(int user_id, const char *file_path)
{
    if (!file_path)
        return 0;

    if (!verify_user_owns_resource(user_id, file_path))
    {
        fprintf(stderr, "[-] User does not own this resource\n");
        return 0;
    }

    char *abs_path = get_absolute_path(user_id, file_path);
    if (!abs_path || !path_exists(abs_path))
        return 0;

    if (unlink(abs_path) != 0)
    {
        perror("[-] Failed to delete file");
        return 0;
    }

    /* Mark as deleted in database */
    char safe_path[512];
    extern MYSQL *conn;
    mysql_real_escape_string(conn, safe_path, (char *)file_path, strlen(file_path));

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE files SET is_deleted=TRUE WHERE file_path='%s' AND owner_id=%d",
             safe_path, user_id);

    db_query(query);

    /* Log activity */
    log_activity(user_id, "DELETE", "FILE", "", file_path, "File deleted", "");

    printf("[+] File deleted: %s\n", file_path);
    return 1;
}

int rename_file(int user_id, const char *old_path, const char *new_path)
{
    if (!old_path || !new_path)
        return 0;

    if (!verify_user_owns_resource(user_id, old_path))
        return 0;

    char *abs_old = get_absolute_path(user_id, old_path);
    char *abs_new = get_absolute_path(user_id, new_path);

    if (!abs_old || !abs_new || !path_exists(abs_old) || path_exists(abs_new))
        return 0;

    if (rename(abs_old, abs_new) != 0)
    {
        perror("[-] Failed to rename file");
        return 0;
    }

    /* Update database */
    char safe_old[512];
    char safe_new[512];
    extern MYSQL *conn;
    mysql_real_escape_string(conn, safe_old, (char *)old_path, strlen(old_path));
    mysql_real_escape_string(conn, safe_new, (char *)new_path, strlen(new_path));

    char query[512];
    snprintf(query, sizeof(query),
             "UPDATE files SET file_path='%s', updated_at=NOW() WHERE file_path='%s' AND owner_id=%d",
             safe_new, safe_old, user_id);

    db_query(query);

    log_activity(user_id, "RENAME", "FILE", "", old_path, new_path, "");

    printf("[+] File renamed: %s -> %s\n", old_path, new_path);
    return 1;
}

int copy_file(int user_id, const char *src_path, const char *dest_path)
{
    if (!src_path || !dest_path)
        return 0;

    if (!verify_user_owns_resource(user_id, src_path))
        return 0;

    char *abs_src = get_absolute_path(user_id, src_path);
    char *abs_dest = get_absolute_path(user_id, dest_path);

    if (!abs_src || !abs_dest || !path_exists(abs_src) || path_exists(abs_dest))
        return 0;

    /* Copy file */
    FILE *src_file = fopen(abs_src, "rb");
    if (!src_file)
        return 0;

    FILE *dest_file = fopen(abs_dest, "wb");
    if (!dest_file)
    {
        fclose(src_file);
        return 0;
    }

    char buffer[8192];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src_file)) > 0)
    {
        fwrite(buffer, 1, bytes, dest_file);
    }

    fclose(src_file);
    fclose(dest_file);

    /* Update database */
    char safe_dest[512];
    char safe_src[512];
    char safe_filename[512];
    extern MYSQL *conn;
    mysql_real_escape_string(conn, safe_dest, (char *)dest_path, strlen(dest_path));
    mysql_real_escape_string(conn, safe_src, (char *)src_path, strlen(src_path));

    const char *filename_ptr = strrchr(dest_path, '/');
    if (!filename_ptr)
        filename_ptr = dest_path;
    else
        filename_ptr++;
    mysql_real_escape_string(conn, safe_filename, (char *)filename_ptr, strlen(filename_ptr));

    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO files (owner_id, filename, file_path, permissions) SELECT owner_id, '%s', '%s', permissions FROM files WHERE file_path='%s'",
             safe_filename, safe_dest, safe_src);

    db_query(query);

    log_activity(user_id, "COPY", "FILE", "", src_path, dest_path, "");

    printf("[+] File copied: %s -> %s\n", src_path, dest_path);
    return 1;
}

int move_file(int user_id, const char *src_path, const char *dest_path)
{
    if (rename_file(user_id, src_path, dest_path))
    {
        log_activity(user_id, "MOVE", "FILE", "", src_path, dest_path, "");
        return 1;
    }
    return 0;
}

int search_file(int user_id, const char *pattern, char *response)
{
    if (!pattern || !response)
        return 0;

    char search_pattern[256];
    snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", pattern);

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT file_id, filename, file_path, file_size FROM files WHERE owner_id=%d AND filename LIKE '%s' AND is_deleted=FALSE LIMIT 50",
             user_id, search_pattern);

    MYSQL_RES *result = db_query_result(query);
    if (!result)
    {
        snprintf(response, BUFFER_SIZE, "ERROR|Search query failed");
        return 0;
    }

    strcpy(response, "OK|");
    MYSQL_ROW row;
    char buffer[256];

    while ((row = mysql_fetch_row(result)) != NULL)
    {
        snprintf(buffer, sizeof(buffer), "%s|%s|%s bytes\n",
                 row[0], row[1], row[3]);
        strcat(response, buffer);
    }

    mysql_free_result(result);
    return 1;
}

int check_permission(int user_id, const char *resource_path, int resource_type, int required_perm)
{
    /* For now, allow if user owns the resource */
    /* TODO: Implement proper permission checking with shared access */
    return verify_user_owns_resource(user_id, resource_path);
}

int set_file_permissions(int user_id, const char *file_path, int permissions)
{
    if (!verify_user_owns_resource(user_id, file_path))
        return 0;

    char *abs_path = get_absolute_path(user_id, file_path);
    if (!abs_path)
        return 0;

    if (chmod(abs_path, permissions) != 0)
    {
        perror("[-] Failed to change permissions");
        return 0;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE files SET permissions=%d WHERE file_path='%s' AND owner_id=%d",
             permissions, file_path, user_id);

    db_query(query);
    return 1;
}

int get_file_info(int user_id, const char *file_path, FileInfo *info)
{
    if (!file_path || !info)
        return 0;

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT file_id, file_size, permissions, created_at FROM files WHERE file_path='%s' AND owner_id=%d",
             file_path, user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    info->file_id = atoi(row[0]);
    info->file_size = atoll(row[1]);
    info->permissions = atoi(row[2]);

    mysql_free_result(result);
    return 1;
}

/* ==================== Directory Operations ==================== */

int create_directory(int user_id, const char *dir_path, int permissions)
{
    if (!dir_path)
        return 0;

    char *abs_path = get_absolute_path(user_id, dir_path);
    if (!abs_path || path_exists(abs_path))
        return 0;

    if (mkdir(abs_path, permissions) != 0)
    {
        perror("[-] Failed to create directory");
        return 0;
    }

    /* Register in database */
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO directories (owner_id, dirname, dir_path, permissions) VALUES (%d, '%s', '%s', %d)",
             user_id, strrchr(dir_path, '/') + 1, dir_path, permissions);

    db_query(query);

    log_activity(user_id, "CREATE_DIR", "DIR", "", dir_path, "Directory created", "");
    printf("[+] Directory created: %s\n", dir_path);
    return 1;
}

int delete_directory(int user_id, const char *dir_path)
{
    if (!dir_path)
        return 0;

    if (!verify_user_owns_resource(user_id, dir_path))
        return 0;

    char *abs_path = get_absolute_path(user_id, dir_path);
    if (!abs_path || !path_exists(abs_path))
        return 0;

    /* Recursively delete directory contents */
    DIR *dir = opendir(abs_path);
    if (!dir)
    {
        perror("[-] Failed to open directory");
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", abs_path, entry->d_name);

        struct stat file_stat;
        if (stat(full_path, &file_stat) == 0)
        {
            if (S_ISDIR(file_stat.st_mode))
            {
                /* Recursive delete subdirectory */
                char sub_rel_path[MAX_PATH_LEN];
                snprintf(sub_rel_path, sizeof(sub_rel_path), "%s/%s", dir_path, entry->d_name);
                delete_directory(user_id, sub_rel_path);
            }
            else
            {
                unlink(full_path);
            }
        }
    }
    closedir(dir);

    if (rmdir(abs_path) != 0)
    {
        perror("[-] Failed to delete directory");
        return 0;
    }

    /* Mark as deleted in database */
    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE directories SET is_deleted=TRUE WHERE dir_path='%s' AND owner_id=%d",
             dir_path, user_id);
    db_query(query);

    log_activity(user_id, "DELETE_DIR", "DIR", "", dir_path, "Directory deleted", "");
    printf("[+] Directory deleted: %s\n", dir_path);
    return 1;
}

int rename_directory(int user_id, const char *old_path, const char *new_path)
{
    if (!old_path || !new_path)
        return 0;

    if (!verify_user_owns_resource(user_id, old_path))
        return 0;

    char *abs_old = get_absolute_path(user_id, old_path);
    char *abs_new = get_absolute_path(user_id, new_path);

    if (!abs_old || !abs_new || !path_exists(abs_old) || path_exists(abs_new))
        return 0;

    if (rename(abs_old, abs_new) != 0)
    {
        perror("[-] Failed to rename directory");
        return 0;
    }

    char query[512];
    snprintf(query, sizeof(query),
             "UPDATE directories SET dir_path='%s', updated_at=NOW() WHERE dir_path='%s' AND owner_id=%d",
             new_path, old_path, user_id);
    db_query(query);

    log_activity(user_id, "RENAME_DIR", "DIR", "", old_path, "Directory renamed", "");
    return 1;
}

int copy_directory(int user_id, const char *src_path, const char *dest_path)
{
    if (!src_path || !dest_path)
        return 0;

    if (!verify_user_owns_resource(user_id, src_path))
        return 0;

    char *abs_src = get_absolute_path(user_id, src_path);
    char *abs_dest = get_absolute_path(user_id, dest_path);

    if (!abs_src || !abs_dest || !path_exists(abs_src) || path_exists(abs_dest))
        return 0;

    /* Create destination directory */
    if (mkdir(abs_dest, DEFAULT_DIR_PERMS) != 0)
        return 0;

    /* Copy contents recursively */
    DIR *dir = opendir(abs_src);
    if (!dir)
        return 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char src_file[MAX_PATH_LEN];
        char dest_file[MAX_PATH_LEN];
        snprintf(src_file, sizeof(src_file), "%s/%s", abs_src, entry->d_name);
        snprintf(dest_file, sizeof(dest_file), "%s/%s", abs_dest, entry->d_name);

        struct stat file_stat;
        if (stat(src_file, &file_stat) == 0)
        {
            if (S_ISDIR(file_stat.st_mode))
            {
                char src_sub[MAX_PATH_LEN];
                char dest_sub[MAX_PATH_LEN];
                snprintf(src_sub, sizeof(src_sub), "%s/%s", src_path, entry->d_name);
                snprintf(dest_sub, sizeof(dest_sub), "%s/%s", dest_path, entry->d_name);
                copy_directory(user_id, src_sub, dest_sub);
            }
            else
            {
                /* Copy file */
                FILE *src = fopen(src_file, "rb");
                FILE *dst = fopen(dest_file, "wb");
                if (src && dst)
                {
                    char buffer[8192];
                    size_t bytes;
                    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0)
                    {
                        fwrite(buffer, 1, bytes, dst);
                    }
                }
                if (src)
                    fclose(src);
                if (dst)
                    fclose(dst);
            }
        }
    }
    closedir(dir);

    log_activity(user_id, "COPY_DIR", "DIR", "", src_path, "Directory copied", "");
    return 1;
}

int move_directory(int user_id, const char *src_path, const char *dest_path)
{
    return rename_directory(user_id, src_path, dest_path);
}

int change_directory(int user_id, const char *new_dir, char *current_dir)
{
    if (!new_dir || !current_dir)
        return 0;

    char *abs_path = get_absolute_path(user_id, new_dir);
    if (!abs_path || !path_exists(abs_path))
        return 0;

    strcpy(current_dir, new_dir);
    return 1;
}

int upload_directory(int user_id, const char *dir_path, const char *dest_path)
{
    /* TODO: Implement streaming of entire directory */
    return 1;
}

int download_directory(int user_id, const char *dir_path)
{
    /* TODO: Implement streaming of entire directory */
    return 1;
}

int set_directory_permissions(int user_id, const char *dir_path, int permissions)
{
    if (!verify_user_owns_resource(user_id, dir_path))
        return 0;

    char *abs_path = get_absolute_path(user_id, dir_path);
    if (!abs_path)
        return 0;

    if (chmod(abs_path, permissions) != 0)
    {
        perror("[-] Failed to change permissions");
        return 0;
    }

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE directories SET permissions=%d WHERE dir_path='%s' AND owner_id=%d",
             permissions, dir_path, user_id);

    db_query(query);
    return 1;
}

int share_file(int user_id, const char *file_path, const char *target_user, int read, int write, int execute)
{
    if (!verify_user_owns_resource(user_id, file_path))
        return 0;

    /* Get target user ID */
    char query[256];
    snprintf(query, sizeof(query), "SELECT user_id FROM users WHERE username='%s'", target_user);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int target_user_id = atoi(row[0]);
    mysql_free_result(result);

    /* Get file ID */
    snprintf(query, sizeof(query),
             "SELECT file_id FROM files WHERE file_path='%s' AND owner_id=%d",
             file_path, user_id);

    result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    row = mysql_fetch_row(result);
    int file_id = atoi(row[0]);
    mysql_free_result(result);

    /* Insert permission */
    snprintf(query, sizeof(query),
             "INSERT INTO file_permissions (file_id, user_id, read_perm, write_perm, execute_perm) "
             "VALUES (%d, %d, %d, %d, %d) ON DUPLICATE KEY UPDATE read_perm=%d, write_perm=%d, execute_perm=%d",
             file_id, target_user_id, read, write, execute, read, write, execute);

    return db_query(query);
}

int share_directory(int user_id, const char *dir_path, const char *target_user, int read, int write, int execute)
{
    if (!verify_user_owns_resource(user_id, dir_path))
        return 0;

    /* Get target user ID */
    char query[256];
    snprintf(query, sizeof(query), "SELECT user_id FROM users WHERE username='%s'", target_user);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int target_user_id = atoi(row[0]);
    mysql_free_result(result);

    /* Get directory ID */
    snprintf(query, sizeof(query),
             "SELECT dir_id FROM directories WHERE dir_path='%s' AND owner_id=%d",
             dir_path, user_id);

    result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    row = mysql_fetch_row(result);
    int dir_id = atoi(row[0]);
    mysql_free_result(result);

    /* Insert permission */
    snprintf(query, sizeof(query),
             "INSERT INTO file_permissions (dir_id, user_id, read_perm, write_perm, execute_perm) "
             "VALUES (%d, %d, %d, %d, %d) ON DUPLICATE KEY UPDATE read_perm=%d, write_perm=%d, execute_perm=%d",
             dir_id, target_user_id, read, write, execute, read, write, execute);

    return db_query(query);
}
