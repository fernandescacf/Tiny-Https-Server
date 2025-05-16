#include <http.h>
#include <http_session.h>
#include "backend/portfolio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

char* portfolios_path = NULL;
char* view_path = NULL;
char* resources_path = NULL;

void get_user_portfolio_path(const char* path, session_t session, /*out*/char* portfolio) {
    const size_t len = strlen(path);
    memcpy(portfolio, path, len);
    http_session_get_username(session, portfolio + len);
    for(size_t i = len; portfolio[i] != 0 ; ++i) {
        if(portfolio[i] == ' ') portfolio[i] = '_';
    }
}

void not_found_reply(http_response_t* response) {
    http_set_response_code(response, HTTP_400_BAD_REQUEST);
    http_set_content_type(response, "text/html", true);
    http_set_body(response, 22, "<h1>404 Not Found</h1>", false);
}

void bad_request_reply(http_response_t* response) {
    http_set_response_code(response, HTTP_400_BAD_REQUEST);
    http_set_content_type(response, "text/html", true);
    http_set_body(response, 24, "<h1>400 Bad Request</h1>", false);
}

void unauthorized_reply(http_response_t* response) {
    http_set_response_code(response, HTTP_401_UNAUTHORIZED);
    http_set_content_type(response, "text/html", true);
    http_set_body(response, 24, "<h1>401 UNAUTHORIZED</h1>", false);
    return;
}

void gen_path(const char* base, const char* append, /*out*/char* path) {
    strcpy(path, base);
    strcat(path, append);
}

size_t get_file(const char* file, char** out) {
    if(out == NULL) return 0;
    FILE* fp = fopen(file, "r");
    if(fp == NULL) return 0;

    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    char* payload = malloc(size);
    fseek(fp, 0, SEEK_SET);
    if(fread(payload, 1, size, fp) != size) size = 0;
    fclose(fp);

    *out = payload;

    return size;
}

void set_content_type(const char* file, http_response_t* response) {
    char content_type[64];
    char* type = strchr(file, '.') + 1;
    if(strncmp(type, "js", 2) == 0 || strncmp(type, "json", 4) == 0) {
        sprintf(content_type, "application/%s", type);
    }
    else {
        sprintf(content_type, "text/%s", type);
    }
    http_set_content_type(response, content_type, true);
}

void crypto_get_resource(http_request_t* request, http_response_t* response) {
    if(request->args != NULL) {
        bad_request_reply(response);
        return;
    }

    if(request->url_suffix == NULL) {
        http_set_response_code(response, HTTP_303_SEE_OTHER);
        http_set_content_type(response, "text/html", true);
        http_set_redirect(response, "/login");
        return;
    }

    char path[256];
    gen_path(resources_path, request->url_suffix, path);

    char* payload;
    size_t size = get_file(path, &payload);
    if(size == 0) {
        not_found_reply(response);
        return;
    }

    http_set_response_code(response, HTTP_200_OK);
    set_content_type(path, response);
    http_set_body(response, size, payload, true);
}

void crypto_get_register(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL) {
        bad_request_reply(response);
        return;
    }

    session_t session = http_session_get(request);
    if(session != INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_303_SEE_OTHER);
        http_set_content_type(response, "text/html", true);
        http_set_redirect(response, "/home");
        return;
    }

    char path[256];
    gen_path(view_path, "register.html", path);

    char* payload;
    size_t size = get_file(path, &payload);
    if(size == 0) {
        not_found_reply(response);
        return;
    }

    http_set_response_code(response, HTTP_200_OK);
    set_content_type(path, response);
    http_set_body(response, size, payload, true);
}

void crypto_post_register(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL || request->body == NULL) {
        bad_request_reply(response);
        return;
    }

    session_t session = http_session_get(request);
    if(session != INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_400_BAD_REQUEST);
        http_set_content_type(response, "text/plain", true);
        return;
    }

    char username[126] = {0};
    char password[126] = {0};
    char* str = strstr(request->body, "username");
    if(str != NULL) {
        str += sizeof("username\":\"") - 1;
        for(size_t i = 0; *str != '\"'; i++) username[i] = *str++;
    
        str = strstr(str, "password");
        if(str != NULL) {
            str += sizeof("password\":\"") - 1;
            for(size_t i = 0; *str != '\"'; i++) password[i] = *str++;
        }
    }

    if(username[0] == 0 || password[0] == 0) {
        bad_request_reply(response);
        return;
    }

    if(http_session_register_user(username, password) == false) {
        bad_request_reply(response);
    }

    // Create a portfolio for the new user
    PortfolioCreate(portfolios_path, username);

    http_set_response_code(response, HTTP_201_CREATED);
    http_set_content_type(response, "application/json", true);
    http_set_body(response, sizeof("{\"message\": \"User registered successfully\"}") - 1, "{\"message\": \"User registered successfully\"}", false);
}

void crypto_get_login(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL) {
        bad_request_reply(response);
        return;
    }

    session_t session = http_session_get(request);
    if(session != INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_303_SEE_OTHER);
        http_set_content_type(response, "text/html", true);
        http_set_redirect(response, "/home");
        return;
    }

    char path[256];
    gen_path(view_path, "login.html", path);

    char* payload;
    size_t size = get_file(path, &payload);
    if(size == 0) {
        not_found_reply(response);
        return;
    }

    http_set_response_code(response, HTTP_200_OK);
    set_content_type(path, response);
    http_set_body(response, size, payload, true);
}

void crypto_post_login(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL || request->body == NULL) {
        bad_request_reply(response);
        return;
    }

    // If there is already a session we first need to logout
    session_t session = http_session_get(request);
    if(session != INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_400_BAD_REQUEST);
        http_set_content_type(response, "text/plain", true);
        return;
    }

    char username[126] = {0};
    char password[126] = {0};
    char* str = strstr(request->body, "username");
    if(str != NULL) {
        str += sizeof("username\":\"") - 1;
        for(size_t i = 0; *str != '\"'; i++) username[i] = *str++;
    
        str = strstr(str, "password");
        if(str != NULL) {
            str += sizeof("password\":\"") - 1;
            for(size_t i = 0; *str != '\"'; i++) password[i] = *str++;
        }
    }

    if(username[0] == 0 || password[0] == 0) {
        bad_request_reply(response);
        return;
    }

    session = http_session_login_user(username, password);
    memset(password, 0, sizeof(password));

    if(session == INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_401_UNAUTHORIZED);
        http_set_content_type(response, "text/html", true);
        http_set_body(response, 24, "<h1>401 UNAUTHORIZED</h1>", false);
        return;
    }

    http_session_set_cookie(session, response);
    http_set_response_code(response, HTTP_302_FOUND);
    http_set_content_type(response, "text/html", true);
    http_set_redirect(response, "/home");
}

void crypto_post_logout(http_request_t* request, http_response_t* response) {
    session_t session = http_session_get(request);
    if(session != INVALID_SESSION_ID) {
        http_session_logout_user(request);
    }
    
    http_set_response_code(response, HTTP_303_SEE_OTHER);
    http_set_content_type(response, "text/html", true);
    http_set_redirect(response, "/login");
}

void crypto_get_home(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL) {
        bad_request_reply(response);
        return;
    }

    session_t session = http_session_get(request);
    if(session == INVALID_SESSION_ID) {
        http_set_response_code(response, HTTP_303_SEE_OTHER);
        http_set_content_type(response, "text/html", true);
        http_set_redirect(response, "/login");
        return;
    }

    char path[256];
    gen_path(view_path, "index.html", path);

    char* payload;
    size_t size = get_file(path, &payload);
    if(size == 0) {
        not_found_reply(response);
        return;
    }

    http_set_response_code(response, HTTP_200_OK);
    http_set_content_type(response, "text/html", true);
    http_set_body(response, size, payload, true);
}

void crypto_get_coins(http_request_t* request, http_response_t* response) {
    session_t session = http_session_get(request);
    if(session == INVALID_SESSION_ID) {
        unauthorized_reply(response);
        return;
    }

    if(request->args == NULL) {
        http_set_response_code(response, HTTP_200_OK);
        http_set_content_type(response, "application/json", true);

        char portfolio[256];
        get_user_portfolio_path(portfolios_path, session, portfolio);
        char* coinsJson = PortfolioGetCoinsList(portfolio);

        http_set_body(response, strlen(coinsJson), coinsJson, true);
    }
    else {
        bad_request_reply(response);
    }
}

void crypto_get_coin(http_request_t* request, http_response_t* response) {
    session_t session = http_session_get(request);
    if(session == INVALID_SESSION_ID) {
        unauthorized_reply(response);
        return;
    }

    if(request->args != NULL) {
        http_set_response_code(response, HTTP_200_OK);
        http_set_content_type(response, "application/json", true);
        char portfolio[256];
        get_user_portfolio_path(portfolios_path, session, portfolio);
        char* json = PortfolioGetCoinTransactions(portfolio, request->args);
        http_set_body(response, strlen(json), json, true);
    }
    else {
        bad_request_reply(response);
    }
}

void crypto_post_coin(http_request_t* request, http_response_t* response) {
    if(request->args != NULL || request->url_suffix != NULL || request->body == NULL) {
        bad_request_reply(response);
        return;
    }

    session_t session = http_session_get(request);
    if(session == INVALID_SESSION_ID) {
        unauthorized_reply(response);
        return;
    }
    char portfolio[256];
    get_user_portfolio_path(portfolios_path, session, portfolio);
    PortfolioAddCoinTransactions(portfolio, request->args, request->body);

    http_set_response_code(response, HTTP_201_CREATED);
    http_set_content_type(response, "application/json", true);
    http_set_body(response, strlen(request->body), request->body, false);
}

size_t get_timeout(const char* str) {
    char* remain = NULL;
    size_t timeout_s = strtoul(optarg, &remain, 10);
    if(remain[0] != '\0' && remain[1] != '\0') {
        return 0;
    }
    switch(remain[0]) {
        case 'h':
            timeout_s *= 60;
        case 'm':
            timeout_s *= 60;
        case 's':
        default:
            return timeout_s;
    }
}

void app_start(int argc, char* argv[], http_server_t* server) {
    char* root_path = NULL;
    char* server_data = NULL;
    size_t session_timeout_s = 0;
    const char* const short_opts = "";
    const struct option long_opts[] = {
        {"root", required_argument, NULL, 'r'},
        {"timeout", required_argument, NULL, 't'},
        {NULL, no_argument, NULL, 0}
    };
    bool parse = true;
    while(parse) {
        switch(getopt_long(argc, argv, short_opts, long_opts, NULL)) {
        case 'r':
            root_path = optarg;
            break;
        case 't':
        {
            session_timeout_s = get_timeout(optarg);
            break;
        }
        case -1:
            parse = false;
            break;
        case '?':
        default:
            parse = false;
            printf("\nUnkown aplication argument: %s\n", optarg);
            break;
        }
    }

    if(root_path == NULL) {
        printf("\nAPP ERROR: no root was specified\n");
        return;
    }
    else {
        portfolios_path = malloc(strlen(root_path) + sizeof("/data/portfolios/"));
        gen_path(root_path, "/data/portfolios/", portfolios_path);

        server_data = malloc(strlen(root_path) + sizeof("/data/.server/"));
        gen_path(root_path, "/data/.server/", server_data);

        view_path = malloc(strlen(root_path) + sizeof("/view/"));
        gen_path(root_path, "/view/", view_path);

        resources_path = malloc(strlen(view_path) + sizeof("resources/"));
        gen_path(view_path, "resources/", resources_path);
    }

    http_session_engine_start(10, server_data, session_timeout_s);

    http_register_method(server, "/", HTTP_GET, crypto_get_resource);
    http_register_method(server, "/login", HTTP_GET, crypto_get_login);
    http_register_method(server, "/login", HTTP_POST, crypto_post_login);
    http_register_method(server, "/logout", HTTP_GET, crypto_post_logout);          // <----- CHANGE!!!!!
    http_register_method(server, "/register", HTTP_GET, crypto_get_register);
    http_register_method(server, "/register", HTTP_POST, crypto_post_register);
    http_register_method(server, "/home", HTTP_GET, crypto_get_home);
    http_register_method(server, "/coins", HTTP_GET, crypto_get_coins);
    http_register_method(server, "/coin", HTTP_GET, crypto_get_coin);
    http_register_method(server, "/coin", HTTP_POST, crypto_post_coin);

    printf("Crypto portfolio server app started\n");
}

void app_stop() {
    http_session_engine_stop();
    printf("\nCrypto portfolio server app terminated\n");
}