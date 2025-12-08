#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <mysql/mysql.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <openssl/sha.h>
#include <ctype.h>

/* ==================== Server Configuration ==================== */
#define PORT 9000
#define MAX_CLIENTS 50
#define BUFFER_SIZE 8192
#define FILE_CHUNK_SIZE 65536 /* 64KB chunks for file transfer */
#define MAX_PATH_LEN 512
#define MAX_USERNAME_LEN 64
#define MAX_PASSWORD_LEN 256
#define SESSION_TIMEOUT 3600 /* 1 hour */
#define MAX_CMD_LEN 32
#define MAX_ARG_LEN 512

/* ==================== Database Configuration ==================== */
#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASSWORD "mypassword"
#define DB_NAME "file_sharing_app"

/* ==================== Permission Definitions (Unix-style) ==================== */
#define PERM_OWNER_READ 256
#define PERM_OWNER_WRITE 128
#define PERM_OWNER_EXEC 64
#define PERM_GROUP_READ 32
#define PERM_GROUP_WRITE 16
#define PERM_GROUP_EXEC 8
#define PERM_OTHER_READ 4
#define PERM_OTHER_WRITE 2
#define PERM_OTHER_EXEC 1

#define DEFAULT_FILE_PERMS 420
#define DEFAULT_DIR_PERMS 493

/* ==================== Command Definitions ==================== */
typedef enum
{
    /* Authentication */
    CMD_REGISTER,
    CMD_LOGIN,
    CMD_LOGOUT,
    CMD_REFRESH_SESSION,

    /* File Operations */
    CMD_UPLOAD_START,
    CMD_UPLOAD_CHUNK,
    CMD_UPLOAD_END,
    CMD_DOWNLOAD_START,
    CMD_DOWNLOAD_CHUNK,
    CMD_DELETE_FILE,
    CMD_RENAME_FILE,
    CMD_COPY_FILE,
    CMD_MOVE_FILE,
    CMD_SEARCH_FILE,

    /* Directory Operations */
    CMD_LIST_DIR,
    CMD_CREATE_DIR,
    CMD_DELETE_DIR,
    CMD_RENAME_DIR,
    CMD_COPY_DIR,
    CMD_MOVE_DIR,
    CMD_CHANGE_DIR,
    CMD_UPLOAD_DIR,
    CMD_DOWNLOAD_DIR,

    /* Permission Management */
    CMD_SET_FILE_PERM,
    CMD_SET_DIR_PERM,
    CMD_SHARE_FILE,
    CMD_SHARE_DIR,
    CMD_GET_FILE_INFO,

    /* System */
    CMD_QUIT,
    CMD_UNKNOWN
} CommandType;

/* ==================== Protocol Message Format ==================== */
typedef enum
{
    RESPONSE_OK,
    RESPONSE_ERROR,
    RESPONSE_DATA,
    RESPONSE_FILE_START,
    RESPONSE_FILE_CHUNK,
    RESPONSE_FILE_END,
    RESPONSE_LIST,
    RESPONSE_SEARCH_RESULT
} ResponseType;

typedef struct
{
    int type;        /* ResponseType */
    int status_code; /* 0 = success, -1 = error */
    char message[256];
    int data_length;
    char data[BUFFER_SIZE];
} MessageHeader;

/* ==================== Data Structures ==================== */
typedef struct
{
    int user_id;
    char username[MAX_USERNAME_LEN];
    char email[128];
    char home_dir[MAX_PATH_LEN];
    int permissions;
} User;

typedef struct
{
    int session_id;
    int user_id;
    char token[256];
    time_t created_at;
    time_t expires_at;
    int is_active;
} Session;

typedef struct
{
    int file_id;
    int owner_id;
    char filename[256];
    char file_path[MAX_PATH_LEN];
    long long file_size;
    char file_hash[65]; /* SHA256 hash */
    int permissions;
    time_t created_at;
    time_t updated_at;
} FileInfo;

typedef struct
{
    int dir_id;
    int owner_id;
    char dirname[256];
    char dir_path[MAX_PATH_LEN];
    int permissions;
    time_t created_at;
    time_t updated_at;
} DirectoryInfo;

typedef struct
{
    int perm_id;
    int file_id;
    int dir_id;
    int user_id;
    int read_perm;
    int write_perm;
    int execute_perm;
} PermissionEntry;

typedef struct
{
    int log_id;
    int user_id;
    char action[64];
    char resource_type[32];
    char resource_name[256];
    char resource_path[MAX_PATH_LEN];
    char details[512];
    time_t created_at;
} ActivityLog;

typedef struct
{
    int socket;
    char username[MAX_USERNAME_LEN];
    int user_id;
    int logged_in;
    char session_token[256];
    time_t login_time;
    char current_dir[MAX_PATH_LEN];
    struct sockaddr_in address;
    socklen_t address_len;
} Client;

typedef struct
{
    int file_id;
    long long total_size;
    long long transferred;
    char file_path[MAX_PATH_LEN];
    int permissions;
} FileTransfer;

/* ==================== Global Variables ==================== */
extern MYSQL *conn;
extern pthread_mutex_t db_lock;
extern pthread_mutex_t log_lock;

/* ==================== Function Declarations ==================== */

/* Database Functions (db.c) */
int db_connect(void);
void db_close(void);
int db_query(const char *query);
MYSQL_RES *db_query_result(const char *query);

/* User Management (user.c) */
int user_register(const char *username, const char *password, const char *email);
int user_login(const char *username, const char *password, char *token);
int user_logout(const char *token);
int user_verify_session(const char *token, int *user_id);
int get_user_info(int user_id, User *user);
char *generate_session_token(int user_id);
void sha1_hash_password(const char *password, char *hash_output);

/* File Operations (fileops.c) */
int ensure_parent_dir(const char *path);
int list_directory(int user_id, const char *path, char *response);
FILE *upload_file_start(int user_id, const char *dest_path, const char *filename, long long file_size);
int upload_file_data(int socket, FILE *fp, long long file_size);
void db_mark_file_uploaded(const char *dest_path);
void db_mark_file_failed(const char *dest_path);
int upload_file_chunk(int user_id, int file_id, const char *data, int chunk_size);
int upload_file_end(int user_id, int file_id, const char *file_hash);
int download_file_start(int user_id, const char *file_path, FileTransfer *transfer);
int download_file_chunk(int user_id, FileTransfer *transfer, char *buffer);
int download_file_end(int user_id, int file_id);
int delete_file(int user_id, const char *file_path);
int rename_file(int user_id, const char *old_path, const char *new_path);
int copy_file(int user_id, const char *src_path, const char *dest_path);
int move_file(int user_id, const char *src_path, const char *dest_path);
int search_file(int user_id, const char *pattern, char *response);

/* Directory Operations */
int create_directory(int user_id, const char *dir_path, int permissions);
int delete_directory(int user_id, const char *dir_path);
int rename_directory(int user_id, const char *old_path, const char *new_path);
int copy_directory(int user_id, const char *src_path, const char *dest_path);
int move_directory(int user_id, const char *src_path, const char *dest_path);
int change_directory(int user_id, const char *new_dir, char *current_dir);
int upload_directory(int user_id, const char *dir_path, const char *dest_path);
int download_directory(int user_id, const char *dir_path);

/* Permission Management */
int set_file_permissions(int user_id, const char *file_path, int permissions);
int set_directory_permissions(int user_id, const char *dir_path, int permissions);
int share_file(int user_id, const char *file_path, const char *target_user, int read, int write, int execute);
int share_directory(int user_id, const char *dir_path, const char *target_user, int read, int write, int execute);
int check_permission(int user_id, const char *resource_path, int resource_type, int required_perm);
int get_file_info(int user_id, const char *file_path, FileInfo *info);

/* Activity Logging (logging.c) */
int log_activity(int user_id, const char *action, const char *resource_type,
                 const char *resource_name, const char *resource_path,
                 const char *details, const char *ip_address);
int get_user_activity_logs(int user_id, char *response);

/* Client Handler (server.c) */
void *client_handler(void *arg);

/* Utility Functions */
int verify_user_owns_resource(int user_id, const char *resource_path);
int is_valid_username(const char *username);
int is_valid_path(const char *path);
char *get_absolute_path(int user_id, const char *relative_path);
int path_exists(const char *path);
long long get_file_size(const char *path);
char *compute_file_hash(const char *file_path);
void string_to_lower(char *str);

#endif /* SERVER_H */
