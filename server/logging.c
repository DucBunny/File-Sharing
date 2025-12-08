#include "server.h"

int log_activity(int user_id, const char *action, const char *resource_type,
                 const char *resource_name, const char *resource_path,
                 const char *details, const char *ip_address)
{
    if (!action || !resource_type)
        return 0;

    pthread_mutex_lock(&log_lock);

    char query[1024];
    char safe_details[512];
    char safe_name[256];
    char safe_path[512];

    /* Escape special characters */
    if (details)
        mysql_real_escape_string(conn, safe_details, details, strlen(details));
    if (resource_name)
        mysql_real_escape_string(conn, safe_name, resource_name, strlen(resource_name));
    if (resource_path)
        mysql_real_escape_string(conn, safe_path, resource_path, strlen(resource_path));

    snprintf(query, sizeof(query),
             "INSERT INTO activity_logs (user_id, action, resource_type, resource_name, resource_path, details, ip_address, created_at) "
             "VALUES (%d, '%s', '%s', '%s', '%s', '%s', '%s', NOW())",
             user_id, action, resource_type, safe_name, safe_path, safe_details, ip_address);

    int result = db_query(query);

    pthread_mutex_unlock(&log_lock);

    return result;
}

int get_user_activity_logs(int user_id, char *response)
{
    if (!response)
        return 0;

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT action, resource_type, resource_path, details, created_at FROM activity_logs "
             "WHERE user_id=%d ORDER BY created_at DESC LIMIT 100",
             user_id);

    MYSQL_RES *result = db_query_result(query);
    if (!result)
    {
        snprintf(response, BUFFER_SIZE, "ERROR|Failed to retrieve logs");
        return 0;
    }

    strcpy(response, "OK|");
    MYSQL_ROW row;
    char buffer[512];

    while ((row = mysql_fetch_row(result)) != NULL)
    {
        snprintf(buffer, sizeof(buffer), "%s|%s|%s|%s|%s\n",
                 row[0], row[1], row[2], row[3], row[4]);
        strcat(response, buffer);
    }

    mysql_free_result(result);
    return 1;
}
