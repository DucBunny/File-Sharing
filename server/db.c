#include "server.h"
#include <errno.h>

extern MYSQL *conn;

int db_connect()
{
    conn = mysql_init(NULL);
    if (!conn)
    {
        fprintf(stderr, "[-] mysql_init() failed\n");
        return 0;
    }

    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASSWORD, DB_NAME, 0, NULL, 0))
    {
        fprintf(stderr, "[-] MySQL connection failed: %s\n", mysql_error(conn));
        return 0;
    }

    printf("[+] Connected to MySQL database successfully\n");
    return 1;
}

void db_close()
{
    if (conn)
    {
        mysql_close(conn);
        printf("[+] Database connection closed\n");
    }
}

int db_query(const char *query)
{
    pthread_mutex_lock(&db_lock);

    if (!query || strlen(query) == 0)
    {
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    if (mysql_query(conn, query))
    {
        fprintf(stderr, "[-] Query failed: %s\n", mysql_error(conn));
        fprintf(stderr, "[-] Query: %s\n", query);
        pthread_mutex_unlock(&db_lock);
        return 0;
    }

    pthread_mutex_unlock(&db_lock);
    return 1;
}

MYSQL_RES *db_query_result(const char *query)
{
    pthread_mutex_lock(&db_lock);

    if (!query || strlen(query) == 0)
    {
        pthread_mutex_unlock(&db_lock);
        return NULL;
    }

    if (mysql_query(conn, query))
    {
        fprintf(stderr, "[-] Query failed: %s\n", mysql_error(conn));
        pthread_mutex_unlock(&db_lock);
        return NULL;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    pthread_mutex_unlock(&db_lock);
    return result;
}

/* ==================== Utility Functions ==================== */

void sha1_hash_password(const char *password, char *hash_output)
{
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA_CTX sha1;
    SHA1_Init(&sha1);
    SHA1_Update(&sha1, (unsigned char *)password, strlen(password));
    SHA1_Final(hash, &sha1);

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        sprintf(hash_output + (i * 2), "%02x", hash[i]);
    }
    hash_output[40] = '\0';
}

char *generate_session_token(int user_id)
{
    static char token[256];
    unsigned char hash[SHA_DIGEST_LENGTH];
    char data[128];

    snprintf(data, sizeof(data), "%d_%ld_%ld", user_id, time(NULL), random());

    SHA_CTX sha1;
    SHA1_Init(&sha1);
    SHA1_Update(&sha1, (unsigned char *)data, strlen(data));
    SHA1_Final(hash, &sha1);

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        sprintf(token + (i * 2), "%02x", hash[i]);
    }
    token[40] = '\0';

    return token;
}

int is_valid_username(const char *username)
{
    if (!username || strlen(username) < 3 || strlen(username) > MAX_USERNAME_LEN - 1)
        return 0;

    for (int i = 0; username[i]; i++)
    {
        if (!isalnum(username[i]) && username[i] != '_' && username[i] != '-')
            return 0;
    }
    return 1;
}

int is_valid_path(const char *path)
{
    if (!path || strlen(path) == 0 || strlen(path) > MAX_PATH_LEN - 1)
        return 0;

    /* Check for path traversal attempts */
    if (strstr(path, "..") != NULL)
        return 0;

    return 1;
}

void string_to_lower(char *str)
{
    for (int i = 0; str[i]; i++)
    {
        str[i] = tolower((unsigned char)str[i]);
    }
}

int path_exists(const char *path)
{
    struct stat buffer;
    return (stat(path, &buffer) == 0);
}

long long get_file_size(const char *path)
{
    struct stat buffer;
    if (stat(path, &buffer) != 0)
        return -1;
    return buffer.st_size;
}

char *compute_file_hash(const char *file_path)
{
    static char hash_str[65];
    unsigned char hash[SHA_DIGEST_LENGTH];
    FILE *file = fopen(file_path, "rb");

    if (!file)
        return NULL;

    SHA_CTX sha1;
    SHA1_Init(&sha1);

    unsigned char buffer[8192];
    size_t bytes;

    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        SHA1_Update(&sha1, buffer, bytes);
    }

    SHA1_Final(hash, &sha1);
    fclose(file);

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
    {
        sprintf(hash_str + (i * 2), "%02x", hash[i]);
    }
    hash_str[40] = '\0';

    return hash_str;
}

char *get_absolute_path(int user_id, const char *relative_path)
{
    static char abs_path[MAX_PATH_LEN];
    char user_home[MAX_PATH_LEN];

    /* Get user's home directory from database */
    char query[256];
    snprintf(query, sizeof(query), "SELECT user_home_dir FROM users WHERE user_id=%d", user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        return NULL;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    strncpy(user_home, row[0], MAX_PATH_LEN - 1);
    mysql_free_result(result);

    /* Build absolute path */
    if (relative_path[0] == '/')
    {
        snprintf(abs_path, sizeof(abs_path), "%s%s", user_home, relative_path);
    }
    else
    {
        snprintf(abs_path, sizeof(abs_path), "%s/%s", user_home, relative_path);
    }

    return abs_path;
}

int verify_user_owns_resource(int user_id, const char *resource_path)
{
    char query[512];

    /* Check files */
    snprintf(query, sizeof(query),
             "SELECT file_id FROM files WHERE owner_id=%d AND file_path='%s' AND is_deleted=FALSE",
             user_id, resource_path);

    MYSQL_RES *result = db_query_result(query);
    if (result && mysql_num_rows(result) > 0)
    {
        mysql_free_result(result);
        return 1;
    }
    if (result)
        mysql_free_result(result);

    /* Check directories */
    snprintf(query, sizeof(query),
             "SELECT dir_id FROM directories WHERE owner_id=%d AND dir_path='%s' AND is_deleted=FALSE",
             user_id, resource_path);

    result = db_query_result(query);
    if (result && mysql_num_rows(result) > 0)
    {
        mysql_free_result(result);
        return 1;
    }
    if (result)
        mysql_free_result(result);

    return 0;
}
