#include <stdio.h>
#include <getopt.h>
#include "cb.h"

void setup_build() {
    create_out_dir("build");
    create_out_dir("build/obj");
    create_out_dir("build/libs");
    create_out_dir("build/bin");
}

void clean_build() {
    remove_dir("build");
}

void init_repo(const char* toolkit) {
    TMP_CONTEXT_PUSH();
    string_t async = {0};
    string_fappend(&async, "%s/%s", toolkit, "async");
    create_dir_tree("libs");
    fetch_local_files(async.data, "libs", NULL);
    TMP_CONTEXT_POP();
}

void sync_repo(const char* toolkit) {
    TMP_CONTEXT_PUSH();
    string_t async = {0};
    string_fappend(&async, "%s/%s", toolkit, "async");
    fetch_local_files(async.data, "libs", NULL);
    TMP_CONTEXT_POP();
}

void build_async_lib() {
    TMP_CONTEXT_PUSH();
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-c", "-fPIC", "-O2", "-Wall", "-o");
        (void)cmd_append_paths(&cmd, "libs");
        (void)cmd_append_libs(&cmd, "pthread");
        build_dir_files(&cmd, "libs", "build/obj/libs");
    }
    builds_t builds = {0};
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "ar");
        (void)cmd_append_args(&cmd, "rcs", "build/libs/async.a");
        (void)cmd_append_files(&cmd, "build/obj/libs/async.o", "build/obj/libs/queue.o", "build/obj/libs/threadpool.o");
        array_append(&builds, build_async(&cmd, NULL));
    }
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-shared", "-fPIC", "-o", "build/libs/async.so");
        (void)cmd_append_files(&cmd, "build/obj/libs/async.o", "build/obj/libs/queue.o", "build/obj/libs/threadpool.o");
        array_append(&builds, build_async(&cmd, NULL));
    }
    builds_wait(&builds);

    TMP_CONTEXT_POP();
}

void build_http_libs() {
    TMP_CONTEXT_PUSH();

    cmd_t cmd = {0};
    cmd_set_build_tool(&cmd, "gcc");
    (void)cmd_append_args(&cmd, "-c", "-O2", "-Wall", "-o");
    (void)cmd_append_paths(&cmd, "libs", "server");
    (void)cmd_append_libs(&cmd, "uuid");
    build_dir_files(&cmd, "http_libs", "build/obj");

    TMP_CONTEXT_POP();
}

void build_server() {
    TMP_CONTEXT_PUSH();
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-c", "-O2", "-Wall", "-o");
        (void)cmd_append_paths(&cmd, "libs");
        (void)cmd_append_libs(&cmd, "ssl");
        build_dir_files(&cmd, "server", "build/obj/server");
    }
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "ar");
        (void)cmd_append_args(&cmd, "rcs", "build/libs/server.a");
        (void)cmd_append_files(&cmd, "build/obj/server/server.o", "build/obj/server/http.o");
        build(&cmd, NULL);
    }
    remove_dir("build/obj/server");
    TMP_CONTEXT_POP();
}

void build_app() {
    TMP_CONTEXT_PUSH();
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-c", "-O2", "-Wall", "-o");
        build_dir_files(&cmd, "app/backend", "build/obj");    
    }
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-c", "-O2", "-Wall", "-o");
        (void)cmd_append_paths(&cmd, "libs", "server", "http_libs");
        (void)cmd_append_files(&cmd, "app/app.c");
        build(&cmd, "build/obj/main.o");
    }
    {
        cmd_t cmd = {0};
        cmd_set_build_tool(&cmd, "gcc");
        (void)cmd_append_args(&cmd, "-o");
        files_append(&cmd.files, "build/obj/", ".o", true);
        (void)cmd_append_files(&cmd, "build/libs/async.a", "build/libs/server.a");
        (void)cmd_append_libs(&cmd, "pthread", "ssl", "uuid");
        build(&cmd, "build/bin/app");
    }
    TMP_CONTEXT_POP();
}

int main(int argc, char* argv[]) {
    rebuild_c_builder(argc, argv);

    const char* const short_opts = "+";
    const struct option long_opts[] = {
        {"clean", no_argument, NULL, 'c'},
        {"skip", no_argument, NULL, 's'},
        {"toolkit", required_argument, NULL, 't'},
        {"init", no_argument, NULL, 'i'},
        {"sync", no_argument, NULL, 'y'},
        {"run", required_argument, NULL, 'r'},
    };
    bool clean = false;
    bool skip = false;
    bool run = false;
    bool init = false;
    bool sync = false;
    char* toolkit = NULL;
    char* exec = NULL;
    int run_argc = 0;
    char** run_argv = 0;

    bool parse = true;
    while(parse) {
        switch(getopt_long(argc, argv, short_opts, long_opts, NULL)) {
        case 'c':
            clean = true;
            break;
        case 's':
            skip = true;
            break;
        case 'i':
            init = true;
            break;
        case 'y':
            sync = true;
            break;
        case 'r':
            run = true;
            exec = optarg;
            break;
        case 't':
            toolkit = optarg;
            break;
        case -1:
            parse = false;
            break;
        case '?':
        default:
            parse = false;
            printf("\nUnkown argument: %s\n", optarg);
            break;
        }
    }
    if(optind < argc) {
        run_argc = argc - optind;
        run_argv = (argv + optind);
    }

    if(clean) clean_build();
    if(init) init_repo(toolkit);
    if(sync) sync_repo(toolkit);
    if(skip) return 0;
  
    setup_build();
    build_async_lib();
    build_server();
    build_http_libs();
    build_app();

    if(run) run_exec(exec, run_argc, run_argv);

    return 0;
}