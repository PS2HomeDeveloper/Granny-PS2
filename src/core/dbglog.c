#include <stdio.h>
#include <stdarg.h>

#include "dbglog.h"

static FILE *g_log_file;
static int g_reported_open_failure;
static int g_log_enabled;

void debug_init(const char *path, int argc, char *argv[])
{
    g_log_enabled = 0;
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    g_reported_open_failure = 0;
    if (!path || !path[0]) {
        g_reported_open_failure = 1;
        printf("Granny: debug log path is empty\n");
        return;
    }
    g_log_file = fopen(path, "w");
    if (!g_log_file) {
        g_reported_open_failure = 1;
        printf("Granny: cannot create %s\n", path);
        return;
    }

    fprintf(g_log_file, "=== Granny PS2 boot log ===\n");
    fprintf(g_log_file, "log_path=%s\n", path);
    fprintf(g_log_file, "argc=%d argv0=%s\n", argc,
            argc > 0 && argv && argv[0] ? argv[0] : "(none)");
    fprintf(g_log_file, "log_initialized=1\n");
    fflush(g_log_file);
    g_log_enabled = 1;
}

void debug_log(const char *format, ...)
{
    va_list arguments;
    if (!g_log_enabled)
        return;
    if (!g_log_file) {
        if (!g_reported_open_failure) {
            printf("Granny: debug log handle is unavailable\n");
            g_reported_open_failure = 1;
        }
        g_log_enabled = 0;
        return;
    }

    va_start(arguments, format);
    vfprintf(g_log_file, format, arguments);
    va_end(arguments);
    fflush(g_log_file);
}
