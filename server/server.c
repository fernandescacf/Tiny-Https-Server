#include "http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#define USAGE_INFO      "USAGE: -p port --pem \"Full chain certificate\" --key \"Private Key\""

extern void app_start(int argc, char* argv[], http_server_t* server);
extern void app_stop();

int main(int argc, char *argv[]) {
    const char* const short_opts = "+p:t:c:vh";
    const struct option long_opts[] = {
            {"port", required_argument, NULL, 'p'},
            {"ip", required_argument, NULL, 'i'},
            {"connections", required_argument, NULL, 'c'},
            {"tasks", required_argument, NULL, 't'},
            {"key", required_argument, NULL, 'k'},
            {"pem", required_argument, NULL, 3},
            {"verbose", no_argument, NULL, 1},
            {"help", no_argument, NULL, 2},
            {NULL, no_argument, NULL, 0}
    };

    int app_argc = 0;
    char** app_argv = NULL;
    char* ip = NULL;
    char* fullchain = NULL;
    char* privatekey = NULL;
    bool parse = true;
    bool verbose = false;
    int port = -1;
    int tasks = 0;
    int connections = 20;
    while(parse) {
        switch(getopt_long(argc, argv, short_opts, long_opts, NULL)) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'i':
            ip = optarg;
            break;
        case 'c':
            connections = atoi(optarg);
            break;
        case 't':
            tasks = atoi(optarg);
            break;
        case 'k':
            privatekey = optarg;
            break;
        case 3:
            fullchain = optarg;
            break;
        case 1:
            verbose = true;
            break;
        case 2:
            printf(USAGE_INFO"\n");
            return 0;
        case -1:
            parse = false;
            break;
        case '?':
            if(optopt != 0) {
                printf("Error: Try 'Server --help' for more information\n");
                return -1;
            }
        default:
            printf("Error: Try 'Server --help' for more information\n");
            return -1;
        }
    }

    // Not used for now:
    (void)verbose;

    // Set argc and argv to pass the remaining arguments to the application
    if(optind < argc) {
        app_argc = argc - (optind - 1);
        app_argv = (argv + (optind - 1));
        optind = 1;
    }

    if(port == -1 || port == 0 || connections < 1 || fullchain == NULL || privatekey == NULL) {
        printf(USAGE_INFO"\n");
        return -1;
    }

    http_server_t* server = http_server_init(ip, port, tasks, fullchain, privatekey);
    app_start(app_argc, app_argv, server);

    if(http_server_start(server, connections) == 0) {
        // Wait for termination request
        sigset_t set;
        int sig;
        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        sigprocmask(SIG_BLOCK, &set, NULL);
        sigwait(&set, &sig);
        // Signal server to terminate
        http_server_stop(server);
        // Only server_clean will terminate the asycn engine so app can terminate
        // any async task it uses as it sees fit
        app_stop();
        http_server_clean(&server);
    }

    return 0;
}