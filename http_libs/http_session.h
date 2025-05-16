#ifndef _HTTP_SESSION_H_
#define _HTTP_SESSION_H_

#include "http.h"
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

#ifndef SIZE_MAX
    #define INVALID_SESSION_ID  ((size_t)-1)
#else
    #define INVALID_SESSION_ID SIZE_MAX
#endif

// Session Id to be stored in cookies
typedef char session_id_t[37];
// Actual session ID to be used to identify the current session
typedef size_t session_t;

/*
 * Try to create a new user, if success return true otherwise false
*/
bool http_session_register_user(const char* name, const char *password);

/*
 * Try to login the sepcified user with the curresponding password
 * In case of success a new session is started
 * Returns the new session
*/
session_t http_session_login_user(const char* name, const char *password);

/*
 * Try to get the session associated to the incoming reqeust 
*/
session_t http_session_get(http_request_t* request);

/*
 * Closes the specified session
*/
void http_session_logout_user(http_request_t* request);

/*
 * Get the session id of the current session
 * If success true is returned and the session id is passed to parameter session_id 
*/
bool http_session_get_id(session_t session, /*out*/session_id_t session_id);

/*
 * Sets the session_id cookie with the current session id
*/
void http_session_set_cookie(session_t session, http_response_t* response);

/*
 * Starts the session manager engine
 * Returns 0 in case of success
*/
int http_session_engine_start(size_t base_capacity, char* server_data, size_t session_timeout_s);

/*
 * Terminates the session engine
 * Call it before exit to ensure that session/user date is saved
*/
void http_session_engine_stop();

/*
 * Get current user name
*/
void http_session_get_username(session_t session, /*out*/char* username);

#endif