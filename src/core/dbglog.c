#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "dbglog.h"

static FILE *g_log_file;
static int g_log_fd = -1;
static int g_reported_open_failure;
static int g_log_enabled;

void debug_init(const char *path, int argc, char *argv[])
{
    const char *active_path = path;
    g_log_enabled = 0;
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    if (g_log_fd >= 0) {
        fileXioClose(g_log_fd);
        g_log_fd = -1;
    }
    g_reported_open_failure = 0;
    if (!path || !path[0]) {
        g_reported_open_failure = 1;
        printf("Granny: debug log path is empty\n");
        return;
    }
    g_log_file = fopen(path, "w");
    if (!g_log_file && strcmp(path, "mass:/debug_log.txt") != 0) {
        active_path = "mass:/debug_log.txt";
        g_log_file = fopen(active_path, "w");
    }
    if (!g_log_file && strcmp(active_path, "debug_log.txt") != 0) {
        active_path = "debug_log.txt";
        g_log_file = fopen(active_path, "w");
    }
    if (!g_log_file) {
        const char *candidates[3] = {path, "mass:/debug_log.txt", "debug_log.txt"};
        unsigned i;
        for (i = 0; i < 3 && g_log_fd < 0; ++i) {
            if (!candidates[i] || !candidates[i][0])
                continue;
            g_log_fd = fileXioOpen(candidates[i], O_WRONLY | O_CREAT | O_TRUNC, 0666);
            if (g_log_fd >= 0)
                active_path = candidates[i];
        }
    }
    if (!g_log_file && g_log_fd < 0) {
        g_reported_open_failure = 1;
        printf("Granny: cannot create debug log at %s\n", path);
        return;
    }

    if (g_log_file) {
        fprintf(g_log_file, "=== Granny PS2 boot log ===\n");
        fprintf(g_log_file, "log_path=%s\n", active_path);
        fprintf(g_log_file, "argc=%d argv0=%s\n", argc,
                argc > 0 && argv && argv[0] ? argv[0] : "(none)");
        fprintf(g_log_file, "log_initialized=1\n");
        fflush(g_log_file);
    } else {
        char header[512];
        int length = snprintf(header, sizeof(header),
                              "=== Granny PS2 boot log ===\nlog_path=%s\nargc=%d argv0=%s\nlog_initialized=1\n",
                              active_path, argc,
                              argc > 0 && argv && argv[0] ? argv[0] : "(none)");
        if (length > 0)
            fileXioWrite(g_log_fd, header, length < (int)sizeof(header) ? length : (int)sizeof(header) - 1);
    }
    g_log_enabled = 1;
}

void debug_log(const char *format, ...)
{
    va_list arguments;
    if (!g_log_enabled)
        return;
    if (!g_log_file && g_log_fd < 0) {
        if (!g_reported_open_failure) {
            printf("Granny: debug log handle is unavailable\n");
            g_reported_open_failure = 1;
        }
        g_log_enabled = 0;
        return;
    }

    va_start(arguments, format);
    if (g_log_file) {
        vfprintf(g_log_file, format, arguments);
        fflush(g_log_file);
    } else {
        char line[768];
        int length = vsnprintf(line, sizeof(line), format, arguments);
        if (length > 0)
            fileXioWrite(g_log_fd, line, length < (int)sizeof(line) ? length : (int)sizeof(line) - 1);
    }
    va_end(arguments);
}
