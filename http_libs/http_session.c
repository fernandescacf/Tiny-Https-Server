#include "http_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <uuid/uuid.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <lock.h>
#include <queue.h>
#include <async.h>

// .server
//  - .meta
//    - size_t: users_count
//  - .users
//    - name, password

#define INVALID_USER_ID             INVALID_SESSION_ID
#define SESSION_DEFAULT_TIMEOUT     (60 * 60)       // 1 hour

typedef struct user_t {
    char name[126];
    char password[126];
    size_t refs;
}user_t;

typedef struct http_session_t {
    session_id_t id;
    size_t expire_s;
    size_t user_ref;
}http_session_t;

const size_t SESSIONS_DEFAULT_CAPACITY = 10;

static struct {
    rwlock_t lock;
    char* server_data;
    FILE* meta_fp;
    FILE* db_fp;
    queue_t* work;
    size_t capacity;
    size_t entries;
    size_t last;
    user_t* users;
}user_manager;

static struct {
    rwlock_t lock;
    volatile bool running;
    size_t session_timeout;
    asyncTask_t* worker;
    size_t capacity;
    size_t entries;
    size_t last;
    http_session_t* sessions;
}session_manager;

/*private:*/ void* session_task(asyncTask_t* self, void* base_entries) {
    size_t user_entries = (size_t)base_entries;
    size_t cycle_counter = 0;
    while(session_manager.running) {
        // we loop 20 * 50ms to allow faster response to a terminate request
        if(cycle_counter == 0) cycle_counter = 20;
        usleep(50000);
        cycle_counter -= 1;
        
        if(cycle_counter == 0) {
            // Check for expired sessions
            rwlock_read_lock(&session_manager.lock);
            size_t buff[session_manager.entries];
            size_t entries = 0;
            size_t count = session_manager.entries;
            for(size_t i = 0; (count > 0) && (i < session_manager.capacity); ++i) {
                if(session_manager.sessions[i].user_ref != INVALID_USER_ID) {
                    count -= 1;
                    if(--session_manager.sessions[i].expire_s == 0) {
                        buff[entries++] = i;
                    }
                }
            }
            rwlock_unlock(&session_manager.lock);
        
            if(entries > 0) {
                // Remove expired sessions
                rwlock_write_lock(&session_manager.lock);
                while(entries > 0) {
                    entries -= 1;
                    http_session_t* session = &session_manager.sessions[buff[entries]];
                    memset(session->id, 0, sizeof(session->id));

                    rwlock_write_lock(&user_manager.lock);
                    user_manager.users[session->user_ref].refs -= 1;
                    rwlock_unlock(&user_manager.lock);

                    session->user_ref = INVALID_USER_ID;
                    if(entries == (session_manager.last - 1)) session_manager.last -= 1;
                }
                rwlock_unlock(&session_manager.lock);
            }
        }
        
        if(!session_manager.running || cycle_counter == 0) {
            size_t new_entries = 0;
            while(!empty(user_manager.work)) {
                size_t user_id = (size_t)pop(user_manager.work);

                rwlock_read_lock(&user_manager.lock);
                fwrite(&user_manager.users[user_id], sizeof(user_t), 1, user_manager.db_fp);
                rwlock_unlock(&user_manager.lock);

                fflush(user_manager.db_fp);
                new_entries += 1;
            }
            if(new_entries > 0) {
                user_entries += new_entries;
                fseek(user_manager.meta_fp, 0, SEEK_SET);
                fwrite(&user_entries, sizeof(user_entries), 1, user_manager.meta_fp);
                fflush(user_manager.meta_fp);
            }
        }
        if(!session_manager.running) printf("\nSession task is out....\n");
    }
    return NULL;
}

/* private: */ size_t user_get(const char* name, bool lock) {
    if(lock) rwlock_read_lock(&user_manager.lock);
    for(size_t entry = 0; entry < user_manager.last; ++entry) {
        if(strcmp(user_manager.users[entry].name, name) == 0){
            if(lock) rwlock_unlock(&user_manager.lock);
            return entry; // unlock
        }
    }
    if(lock) rwlock_unlock(&user_manager.lock);
    return INVALID_USER_ID;
}

/* private: */ session_t http_session_get_by_id(const session_id_t id) {
    rwlock_read_lock(&session_manager.lock);
    for(size_t i = 0; i < session_manager.capacity; ++i) {
        if(session_manager.sessions[i].id[0] != 0 && strcmp(session_manager.sessions[i].id, id) == 0) {
            rwlock_unlock(&session_manager.lock);
            return (session_t)i;
        }
    }
    rwlock_unlock(&session_manager.lock);
    return INVALID_SESSION_ID;
}

bool http_session_register_user(const char* name, const char *password) {
    // Note: we have to write lock it here to ensure that a parallel request does not register the same user name
    //       and we will have to keep it locked until the users[] is updated for the same reason
    //       to free the lock as soon as possible the write back to the file db will be done in the session task
    rwlock_write_lock(&user_manager.lock);
    if(user_get(name, false) != INVALID_USER_ID) return false; // unlock

    size_t entry;
    if(user_manager.entries < user_manager.capacity) {
        for(entry = 0; entry < user_manager.last; ++entry) {
            if(user_manager.users[entry].name[0] == 0) break;
        }
    }
    else {
        entry = user_manager.last;
        // Expand the users array
        user_manager.capacity *= 2;
        user_manager.users = (user_t*)realloc(user_manager.users, sizeof(user_t) * user_manager.capacity);
        memset(&user_manager.users[user_manager.last], 0, sizeof(user_t) * (user_manager.capacity - session_manager.last));
    }

    user_t* user = &user_manager.users[entry];
    if(entry == user_manager.last) user_manager.last += 1;
    user_manager.entries += 1;

    strncpy(user->name, name, sizeof(user->name));
    strncpy(user->password, password, sizeof(user->password));
    rwlock_unlock(&user_manager.lock);

    // Send the db managing work to be done in the session task
    push(user_manager.work, (void*)entry);

    return true;
}

session_t http_session_login_user(const char* name, const char *password) {
    size_t user_ref;
    if((user_ref = user_get(name,true)) == INVALID_USER_ID || strcmp(user_manager.users[user_ref].password, password) != 0)
        return INVALID_SESSION_ID;
    
    rwlock_write_lock(&session_manager.lock);
    size_t entry;
    if(session_manager.entries < session_manager.capacity) {
        for(entry = 0; entry < session_manager.last; ++entry) {
            if(session_manager.sessions[entry].user_ref == INVALID_USER_ID) break;
        }
    }
    else {
        entry = session_manager.last;
        // Expand the sessions array
        session_manager.capacity *= 2;
        session_manager.sessions = (http_session_t*)realloc(session_manager.sessions, sizeof(http_session_t) * session_manager.capacity);
        memset(&session_manager.sessions[session_manager.last], 0, sizeof(http_session_t) * (session_manager.capacity - session_manager.last));
        for(size_t i = session_manager.last; i < session_manager.capacity; ++i) {
            session_manager.sessions[i].user_ref = INVALID_USER_ID;
        }
    }

    http_session_t* session = &session_manager.sessions[entry];
    if(entry == session_manager.last) session_manager.last += 1;
    session_manager.entries += 1;
    session->user_ref = user_ref;
    session->expire_s = session_manager.session_timeout;
    rwlock_unlock(&session_manager.lock);

    uuid_t binuuid;
    uuid_generate_random(binuuid);
    uuid_unparse_lower(binuuid, session->id);

    rwlock_write_lock(&user_manager.lock);
    user_manager.users[user_ref].refs += 1;
    rwlock_unlock(&user_manager.lock);

    return (session_t)entry;
}

session_t http_session_get(http_request_t* request) {
    session_id_t id;
    const char* rcv_id = http_get_cookie(request, "session_id");

    // If there is no session id on the request we can't identify the session
    if(rcv_id == NULL) return INVALID_SESSION_ID;
    
    memcpy(id, rcv_id, sizeof(id) - 1);
    id[36] = 0;

    return http_session_get_by_id(id);
}

void http_session_logout_user(http_request_t* request) {
    // Try to get current session
    session_t entry = http_session_get(request);
    // If entry is invalid we fuck up xD
    if(entry == INVALID_SESSION_ID) {
        printf("\nERROR in http_session_logout_user: invalid id we fuck up xD\n");
        return;
    }

    rwlock_write_lock(&session_manager.lock);
    http_session_t* session = &session_manager.sessions[entry];
    memset(session->id, 0, sizeof(session->id));

    rwlock_write_lock(&user_manager.lock);
    user_manager.users[session->user_ref].refs -= 1;
    rwlock_unlock(&user_manager.lock);

    session->user_ref = INVALID_USER_ID;
    if(entry == (session_manager.last - 1)) session_manager.last -= 1;
    rwlock_unlock(&session_manager.lock);
}

bool http_session_get_id(session_t session, /*out*/session_id_t session_id) {
    // We are trusting the user of this function xD
    // read lock
    memcpy(session_id, session_manager.sessions[session].id, sizeof(session_id_t) - 1);
    // unlock
    session_id[36] = 0;
    return true;
}

void http_session_set_cookie(session_t session, http_response_t* response) {
    char cookie_value[256];
    session_id_t id;
    http_session_get_id(session, id);
    memcpy(cookie_value + sizeof(id), "; HttpOnly; Secure; SameSite=Strict", sizeof("; HttpOnly; Secure; SameSite=Strict"));
    memcpy(cookie_value, id, sizeof(id));
    http_set_cookie(response, "session_id", cookie_value);
}

/*private:*/ void load_user_db(char* server_data, size_t default_capacity) {
    user_manager.server_data = server_data;
    // Check if .sever foulder exists
    char temp_path[256];
    size_t len = strlen(server_data);

    // Check if .server exists (we only check this folder previous ones have to alreayd exist!)
    DIR* dir = opendir(server_data);
    if(dir == NULL) {
        mkdir(server_data, 0777);
        // Create .server files
        memcpy(temp_path, server_data, len);
        memcpy(temp_path + len, ".meta.bin", sizeof(".meta.bin"));
        user_manager.meta_fp = fopen(temp_path, "w");
        size_t user_count = 0;
        fwrite(&user_count, sizeof(size_t), 1, user_manager.meta_fp);
        user_manager.meta_fp = freopen(temp_path, "r+", user_manager.meta_fp);
        // Create users.db
        memcpy(temp_path + len, "users.db", sizeof("users.db"));
        user_manager.db_fp = fopen(temp_path, "a+");

        user_manager.capacity = default_capacity;
        user_manager.users = (user_t*)calloc(user_manager.capacity, sizeof(user_t));
    }
    else {
        memcpy(temp_path, server_data, len);
        memcpy(temp_path + len, ".meta.bin", sizeof(".meta.bin"));
        user_manager.meta_fp = fopen(temp_path, "r+");
        
        fread(&user_manager.entries, sizeof(user_manager.entries), 1, user_manager.meta_fp);

        memcpy(temp_path + len, "users.db", sizeof("users.db"));
        if(user_manager.entries == 0) {
            // Just make sure the users_db is clena
            user_manager.db_fp = fopen(temp_path, "w");
            user_manager.db_fp = freopen(temp_path, "a+", user_manager.db_fp);
        }
        else {
            user_manager.db_fp = fopen(temp_path, "a+");
        }

        user_manager.last = user_manager.entries;
        user_manager.capacity = user_manager.entries + default_capacity;
        user_manager.users = (user_t*)calloc(user_manager.capacity, sizeof(user_t));

        fread(user_manager.users, sizeof(user_t), user_manager.entries, user_manager.db_fp);
    }
    closedir(dir);
}

int http_session_engine_start(size_t base_capacity, char* server_data, size_t session_timeout_s) {
    if(session_manager.running) return -1;

    session_manager.capacity = ((base_capacity == 0) ? SESSIONS_DEFAULT_CAPACITY : base_capacity);
    session_manager.sessions = (http_session_t*)calloc(session_manager.capacity, sizeof(http_session_t));

    for(size_t i = 0; i < session_manager.capacity; ++i) {
        session_manager.sessions[i].user_ref = INVALID_USER_ID;
    }

    // Also start the users manager TODO: in future decouple the user from the session
    load_user_db(server_data, session_manager.capacity);

    rwlock_init(&session_manager.lock);
    rwlock_init(&user_manager.lock);

    user_manager.work = queue_create(session_manager.capacity * 2);

    session_manager.session_timeout = (session_timeout_s == 0) ? SESSION_DEFAULT_TIMEOUT : session_timeout_s;
    session_manager.running = true;
    session_manager.worker = async(session_task, (void*)user_manager.entries);

    return 0;
}

void http_session_engine_stop() {
    // Signal the session timeout task to terminate
    session_manager.running = false;

    await(&session_manager.worker);

    queue_destroy(&user_manager.work);

    fclose(user_manager.db_fp);
    fclose(user_manager.meta_fp);

    free(user_manager.server_data);
    free(user_manager.users);
    free(session_manager.sessions);

    rwlock_destroy(&session_manager.lock);
    rwlock_destroy(&user_manager.lock);
}

void http_session_get_username(session_t session, /*out*/char* username) {
    rwlock_write_lock(&session_manager.lock);
    rwlock_write_lock(&user_manager.lock);
    strcpy(username, user_manager.users[session_manager.sessions[session].user_ref].name);
    rwlock_unlock(&user_manager.lock);
    rwlock_unlock(&session_manager.lock);
}