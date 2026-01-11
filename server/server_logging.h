#ifndef SERVER_LOGGING_H
#define SERVER_LOGGING_H

#include <pthread.h>

// Mutex for thread-safe logging
extern pthread_mutex_t log_mutex;

// Logging functions
void write_log(const char *format, ...);

#endif // SERVER_LOGGING_H
