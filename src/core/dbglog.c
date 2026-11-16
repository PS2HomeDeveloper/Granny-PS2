#include "dbglog.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEBUG_PATH_CAP 512
#define DEBUG_LINE_CAP 1024

static char g_log_path[DEBUG_PATH_CAP];
static int g_log_enabled;
static int g_reported_open_failure;
static const char g_fallback_log_path[] = "mass:/debug_log.txt";

static int write_record(const char *text, size_t length, int truncate_file)
{
    int fd;
    size_t offset = 0;
    int flags = O_WRONLY | O_CREAT | (truncate_file ? O_TRUNC : O_APPEND);

    fd = open(g_log_path, flags, 0666);
    if (fd < 0)
        return 0;

    while (offset < length) {
        ssize_t written = write(fd, text + offset, length - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            close(fd);
            return 0;
        }
        offset += (size_t)written;
    }

    /* Closing each record forces the newlib/fileXio path to commit the handle. */
    if (close(fd) < 0)
        return 0;
    return 1;
}

void debug_init(const char *path, int argc, char *argv[])
{
    char header[DEBUG_LINE_CAP];
    int length;

    g_log_enabled = 0;
    g_reported_open_failure = 0;
    g_log_path[0] = '\0';

    if (!path || !path[0] || strlen(path) >= sizeof(g_log_path)) {
        g_reported_open_failure = 1;
        printf("Granny: debug log path is empty or too long\n");
        return;
    }
    memcpy(g_log_path, path, strlen(path) + 1);

    length = snprintf(header, sizeof(header),
                      "=== Granny PS2 boot log ===\nlog_path=%s\nargc=%d argv0=%s\nlog_initialized=1\n",
                      g_log_path, argc,
                      argc > 0 && argv && argv[0] ? argv[0] : "(none)");
    if (length < 0 || (size_t)length >= sizeof(header)) {
        g_reported_open_failure = 1;
        printf("Granny: debug log header is too long\n");
        return;
    }
    if (!write_record(header, (size_t)length, 1)) {
        if (strcmp(g_log_path, g_fallback_log_path) != 0) {
            memcpy(g_log_path, g_fallback_log_path, sizeof(g_fallback_log_path));
            length = snprintf(header, sizeof(header),
                              "=== Granny PS2 boot log ===\nlog_path=%s (fallback)\nargc=%d argv0=%s\nlog_initialized=1\n",
                              g_log_path, argc,
                              argc > 0 && argv && argv[0] ? argv[0] : "(none)");
            if (length >= 0 && (size_t)length < sizeof(header) &&
                write_record(header, (size_t)length, 1)) {
                g_log_enabled = 1;
                printf("Granny: sibling log path failed; using %s\n", g_log_path);
                return;
            }
        }
        g_reported_open_failure = 1;
        printf("Granny: cannot create/write debug log (errno=%d)\n", errno);
        return;
    }
    g_log_enabled = 1;
}

void debug_log(const char *format, ...)
{
    char line[DEBUG_LINE_CAP];
    va_list arguments;
    int length;

    if (!g_log_enabled || !format)
        return;

    va_start(arguments, format);
    length = vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    if (length < 0) {
        g_log_enabled = 0;
        return;
    }
    if ((size_t)length >= sizeof(line)) {
        static const char suffix[] = " [log line truncated]\n";
        size_t suffix_size = sizeof(suffix) - 1;
        length = (int)(sizeof(line) - suffix_size - 1);
        memcpy(line + length, suffix, suffix_size);
        length += (int)suffix_size;
    }
    if (!write_record(line, (size_t)length, 0)) {
        if (strcmp(g_log_path, g_fallback_log_path) != 0) {
            memcpy(g_log_path, g_fallback_log_path, sizeof(g_fallback_log_path));
            if (write_record(line, (size_t)length, 1)) {
                printf("Granny: sibling log write failed; continuing at %s\n",
                       g_log_path);
                return;
            }
        }
        if (!g_reported_open_failure) {
            printf("Granny: debug log write failed for %s (errno=%d)\n",
                   g_log_path, errno);
            g_reported_open_failure = 1;
        }
        g_log_enabled = 0;
    }
}
