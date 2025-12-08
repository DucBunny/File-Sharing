#include "server.h"
#include <errno.h>

int user_register(const char *username, const char *password, const char *email)
{
    if (!is_valid_username(username))
    {
        fprintf(stderr, "[-] Invalid username format\n");
        return 0;
    }

    if (strlen(password) < 6)
    {
        fprintf(stderr, "[-] Password must be at least 6 characters\n");
        return 0;
    }

    /* Check if username already exists */
    char check_query[256];
    snprintf(check_query, sizeof(check_query), "SELECT user_id FROM users WHERE username='%s'", username);

    MYSQL_RES *result = db_query_result(check_query);
    if (result && mysql_num_rows(result) > 0)
    {
        mysql_free_result(result);
        fprintf(stderr, "[-] Username already exists\n");
        return 0;
    }
    if (result)
        mysql_free_result(result);

    /* Hash password */
    char password_hash[65];
    sha1_hash_password(password, password_hash);

    /* Create user home directory */
    char user_home[MAX_PATH_LEN];
    snprintf(user_home, sizeof(user_home), "./server_files/%s", username);

    if (mkdir(user_home, DEFAULT_DIR_PERMS) != 0 && errno != EEXIST)
    {
        perror("[-] Failed to create user home directory");
        return 0;
    }

    /* Insert into database */
    char query[512];
    snprintf(query, sizeof(query),
             "INSERT INTO users (username, password_hash, email, user_home_dir) VALUES ('%s', '%s', '%s', '%s')",
             username, password_hash, email ? email : "", user_home);

    if (!db_query(query))
    {
        fprintf(stderr, "[-] Registration failed\n");
        return 0;
    }

    printf("[+] User '%s' registered successfully\n", username);
    return 1;
}

int user_login(const char *username, const char *password, char *token_output)
{
    if (!username || !password || !token_output)
        return 0;

    /* Hash the provided password */
    char password_hash[65];
    sha1_hash_password(password, password_hash);

    /* Query database for user */
    char query[256];
    snprintf(query, sizeof(query),
             "SELECT user_id, password_hash FROM users WHERE username='%s'",
             username);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        fprintf(stderr, "[-] User '%s' not found\n", username);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    int user_id = atoi(row[0]);
    const char *stored_hash = row[1];

    if (strcmp(password_hash, stored_hash) != 0)
    {
        mysql_free_result(result);
        fprintf(stderr, "[-] Invalid password for user '%s'\n", username);
        return 0;
    }

    mysql_free_result(result);

    /* Generate session token */
    char *token = generate_session_token(user_id);
    strcpy(token_output, token);

    /* Insert session into database */
    time_t now = time(NULL);
    time_t expires = now + SESSION_TIMEOUT;

    snprintf(query, sizeof(query),
             "INSERT INTO sessions (user_id, token, created_at, expires_at, is_active) VALUES (%d, '%s', FROM_UNIXTIME(%ld), FROM_UNIXTIME(%ld), TRUE)",
             user_id, token, now, expires);

    if (!db_query(query))
    {
        fprintf(stderr, "[-] Failed to create session\n");
        return 0;
    }

    /* Update last login time */
    snprintf(query, sizeof(query),
             "UPDATE users SET last_login=FROM_UNIXTIME(%ld) WHERE user_id=%d",
             now, user_id);
    db_query(query);

    printf("[+] User '%s' logged in successfully (ID: %d)\n", username, user_id);
    return 1;
}

int user_logout(const char *token)
{
    if (!token)
        return 0;

    char query[256];
    snprintf(query, sizeof(query),
             "UPDATE sessions SET is_active=FALSE WHERE token='%s'",
             token);

    return db_query(query);
}

int user_verify_session(const char *token, int *user_id_output)
{
    if (!token || !user_id_output)
        return 0;

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT user_id FROM sessions WHERE token='%s' AND is_active=TRUE AND expires_at > NOW()",
             token);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    *user_id_output = atoi(row[0]);
    mysql_free_result(result);

    return 1;
}

int get_user_info(int user_id, User *user)
{
    if (!user)
        return 0;

    char query[256];
    snprintf(query, sizeof(query),
             "SELECT user_id, username, email, user_home_dir FROM users WHERE user_id=%d",
             user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result || mysql_num_rows(result) == 0)
    {
        if (result)
            mysql_free_result(result);
        return 0;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    user->user_id = atoi(row[0]);
    strncpy(user->username, row[1], MAX_USERNAME_LEN - 1);
    strncpy(user->email, row[2], 127);
    strncpy(user->home_dir, row[3], MAX_PATH_LEN - 1);
    user->permissions = DEFAULT_DIR_PERMS;

    mysql_free_result(result);
    return 1;
}
