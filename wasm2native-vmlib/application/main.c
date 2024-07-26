#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bh_platform.h"
#include "w2n_export.h"

static int app_argc;
static char **app_argv;

/* clang-format off */
static int
print_help(const char *app_name)
{
    os_printf("Usage: %s [options] [args...]\n", app_name);
    os_printf("options:\n");
    os_printf("  --invoke <name>   Specify a function name of the module to run\n");
    os_printf("                    rather than main\n");
    os_printf("  --repl            Start a simple REPL (read-eval-print-loop) mode\n");
    os_printf("                    that runs commands in the form of \"FUNC ARG...\"\n");
    os_printf("  --help            Show this help info\n");

    return 1;
}
/* clang-format on */

bool
wasm_application_execute_main(int32 argc, char **argv);

bool
wasm_application_execute_func(const char *func_name, int32 argc, char **argv);

static bool
app_instance_main()
{
    return wasm_application_execute_main(app_argc, app_argv);
}

static bool
app_instance_func(const char *func_name)
{
    /* The result of wasm function or exception info was output inside
       wasm_application_execute_func(), here we don't output them again. */
    return wasm_application_execute_func(func_name, app_argc - 1, app_argv + 1);
}

/**
 * Split a string into an array of strings
 * Returns NULL on failure
 * Memory must be freed by caller
 * Based on: http://stackoverflow.com/a/11198630/471795
 */
static char **
split_string(char *str, int *count, const char *delimer)
{
    char **res = NULL, **res1;
    char *p;
    int idx = 0;

    /* split string and append tokens to 'res' */
    do {
        p = strtok(str, delimer);
        str = NULL;
        res1 = res;
        res = (char **)realloc(res1, sizeof(char *) * (uint32)(idx + 1));
        if (res == NULL) {
            free(res1);
            return NULL;
        }
        res[idx++] = p;
    } while (p);

    /**
     * Due to the function name,
     * res[0] might contain a '\' to indicate a space
     * func\name -> func name
     */
    p = strchr(res[0], '\\');
    while (p) {
        *p = ' ';
        p = strchr(p, '\\');
    }

    if (count) {
        *count = idx - 1;
    }
    return res;
}

static void
app_instance_repl()
{
    char *cmd = NULL;
    size_t len = 0;
    ssize_t n;

    while ((os_printf("webassembly> "), fflush(stdout),
            n = getline(&cmd, &len, stdin))
           != -1) {
        bh_assert(n > 0);
        if (cmd[n - 1] == '\n') {
            if (n == 1)
                continue;
            else
                cmd[n - 1] = '\0';
        }
        if (!strcmp(cmd, "__exit__")) {
            os_printf("exit repl mode\n");
            break;
        }
        app_argv = split_string(cmd, &app_argc, " ");
        if (app_argv == NULL) {
            os_printf("prepare func params failed: split string failed.\n");
            break;
        }
        if (app_argc != 0) {
            bool ret;

            ret = wasm_application_execute_func(app_argv[0], app_argc - 1,
                                                app_argv + 1);
            if (!ret) {
                os_printf("Exception: %s\n", wasm_get_exception_msg());
            }
        }
        free(app_argv);
    }

    free(cmd);
}

int
main(int argc, char *argv[])
{
    const char *func_name = NULL, *exce_msg;
    char *app_name = argv[0], **argv1 = NULL;
    int32 exce_id, i, ret = -1, argc1;
    bool is_repl_mode = false;

    /* Process options. */
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
        if (!strcmp(argv[0], "--invoke")) {
            argc--, argv++;
            if (argc < 1) {
                return print_help(app_name);
            }
            func_name = argv[0];
        }
        else if (!strcmp(argv[0], "--repl")) {
            is_repl_mode = true;
        }
        else if (!strcmp(argv[0], "--help")) {
            return print_help(app_name);
        }
        else {
            break;
        }
    }

    argc1 = argc + 1;
    if (!(argv1 = malloc(sizeof(char *) * argc1))) {
        os_printf("Allocate memory failed.");
        goto fail;
    }
    argv1[0] = app_name;
    for (i = 0; i < argc; i++) {
        argv1[i + 1] = argv[i];
    }

    app_argc = argc1;
    app_argv = argv1;

    wasm_instance_create();

    if (!wasm_instance_is_created()) {
        os_printf("Create wasm instance failed.");
        goto fail;
    }

    ret = 0;
    if (is_repl_mode) {
        app_instance_repl();
    }
    else if (func_name) {
        if (!app_instance_func(func_name))
            ret = 1;
    }
    else {
        if (!app_instance_main())
            ret = 1;
    }

    if (ret) {
        exce_id = wasm_get_exception();
        if (exce_id) {
            exce_msg = wasm_get_exception_msg();
            os_printf("Exception: %s\n", exce_msg);
        }
        else {
            os_printf("Exception: unknown error\n");
        }
    }

fail:
    wasm_instance_destroy();

    if (argv1)
        free(argv1);
    return ret;
}

#if 0
void print_i32(uint32 i32)
{
    printf("##i32: %u\n", i32);
}

void print_i64(uint64 i64)
{
    printf("##i64: %lu\n", i64);
}
#endif
