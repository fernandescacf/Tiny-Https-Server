#ifndef _BUILDER_H_
#define _BUILDER_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <time.h>
#include <fcntl.h>

#define ARRAY_IMPLEMENTATIONS
#define STRING_IMPLEMENTATIONS
#define ARENA_IMPLEMENTATIONS
#define CONTEXT_MEMORY_IMPLEMENTATIONS
#define ARRAY_CUSTOM_MEMORY_ALLOCATOR
#define STRING_CUSTOM_MEMORY_ALLOCATOR

extern void* array_realloc_memory(void* addr, size_t old_size, size_t new_size);
extern void array_free_memory(void* addr);

#ifndef ARRAY_CUSTOM_MEMORY_ALLOCATOR
#ifdef ARRAY_IMPLEMENTATIONS
    void* array_realloc_memory(void* addr, size_t old_size, size_t new_size) {
        (void) old_size;
        return realloc(addr, new_size);
    }
    void array_free_memory(void* addr) {
        free(addr);
    }
#endif
#endif

#define ARRAY(type) struct {type *data; size_t size; size_t capacity;}
#define DEFINE_ARRAY_TYPE(name, type) typedef struct {type *data; size_t size; size_t capacity;} name;

#define array_append_many(array, ...) \
    do {\
        typeof(*(array)->data) __src_data[] = {__VA_ARGS__}; \
        _array_append_many(array, __src_data, sizeof(*(array)->data), ARRAY_COUNT_ARGS(__VA_ARGS__));\
    }while(0)
#define array_append_carray(array, items, count) _array_append_many(array, items, sizeof(*(array)->data), count)
#define array_append_array(array, src) if((src)->size)_array_append_many(array, (src)->data, sizeof(*(array)->data), (src)->size)
#define array_append(array, item) \
    do {\
        typeof(*(array)->data) __src_data[] = {item}; \
        _array_append_many(array, __src_data, sizeof(*(array)->data), 1);\
    }while(0)
#define array_free(array)  do{ array_free_memory((array)->data); ((array)->size = 0); ((array)->capacity = 0); }while(0)
#define array_clear(array)  ((array)->size = 0)
#define array_reserve(array, reserve) _array_resize(array, sizeof(*(array)->data), reserve)
#define array_resize(array, size) do{ if((array)->size > size){(array)->size = size;} else{ array_reserve(array, size); (array)->size = size; } }while(0)

#define array_foreach(it, array) for(typeof((array)->data) it = (array)->data; it < ((array)->data + (array)->size); ++it)
#define array_iterator_index(it, array) (((size_t)it - (size_t)(array)->data) / sizeof(*(array)->data))

#define ARRAY_COUNT_ARGS(...)_ARRAY_COUNT_ARGS(__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _ARRAY_COUNT_ARGS(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N

extern void _array_append_many(void* array, void* src, size_t type_size, size_t count);
extern void _array_resize(void* array, size_t type_size, size_t size);

#ifdef ARRAY_IMPLEMENTATIONS
void _array_append_many(void* array, void* src, size_t type_size, size_t count) {
    struct {char* data; size_t size; size_t capacity;}* g_array = array;
    if(g_array->capacity < g_array->size + count) {
#ifdef ARRAY_EXPONENTIAL_GROWTH
        if(count > g_array->capacity) g_array->capacity += count;
        else g_array->capacity *= 2;
#else
        g_array->capacity += count;
#endif
        g_array->data = array_realloc_memory(g_array->data, g_array->size * type_size, g_array->capacity * type_size);
    }
    memcpy(g_array->data + (g_array->size * type_size), src, count * type_size);
    g_array->size += count;
}

void _array_resize(void* array, size_t type_size, size_t size) {
    struct {char* data; size_t size; size_t capacity;}* g_array = array;
    if(g_array->capacity < size) {
        g_array->capacity = size;
        g_array->data = array_realloc_memory(g_array->data, g_array->size * type_size, g_array->capacity * type_size);
    }
}
#endif


typedef struct arena_t {
    size_t size;
    size_t free_offset;
    char* data;
}arena_t;

#ifndef TEMP_DEFAULT_SIZE
    #define TEMP_DEFAULT_SIZE   (32 * 1024 * 1024)
#endif
#define WORD_SIZE sizeof(void *)
#define ALIGN_TO_WORD_SIZE(size) ((size < WORD_SIZE) ? (WORD_SIZE) : ((size + (WORD_SIZE - 1)) & ~(WORD_SIZE - 1)))

extern void* arena_reserve_memory(size_t size);
extern void* arena_get_memory(arena_t* arena, size_t size);
extern void arena_flush(arena_t* arena);
extern void arena_reset(arena_t* arena);

#ifdef ARENA_IMPLEMENTATIONS
void* arena_reserve_memory(size_t size) {
    return mmap(NULL, size, PROT_WRITE|PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
}

void* arena_get_memory(arena_t* arena, size_t size) {
    if(arena->data == NULL) {
        arena->data = arena_reserve_memory(TEMP_DEFAULT_SIZE);
        if(arena->data) {
            arena->size = TEMP_DEFAULT_SIZE;
            arena->free_offset = 0;
        }
        else return NULL;
    }
    size_t to_alloc = ALIGN_TO_WORD_SIZE(size);
    if((arena->size - arena->free_offset) < to_alloc) return NULL;
    void* addr = &arena->data[arena->free_offset];
    arena->free_offset += to_alloc;
    return addr;
}

void arena_flush(arena_t* arena) {
    if(arena && arena->data) {
        munmap(arena->data, arena->size);
        arena->free_offset = 0;
        arena->size = 0;
        arena->data = NULL;
    }
}

void arena_reset(arena_t* arena) {
    if(arena) {
        arena->free_offset = 0;
    }
}
#endif


extern void* string_realloc_memory(void* addr, size_t old_size, size_t new_size);
extern void string_free_memory(void* addr);

#ifndef STRING_CUSTOM_MEMORY_ALLOCATOR
#ifdef STRING_IMPLEMENTATIONS
    void* string_realloc_memory(void* addr, size_t old_size, size_t new_size) {
        (void)old_size;
        return realloc(addr, new_size);
    }
    void string_free_memory(void* addr) {
        free(addr);
    }
#endif
#endif

// NOTE: size doesn't include '/0'
typedef struct string_t {
    size_t size;
    size_t capacity;
    char* data;
}string_t;

#define string_append_many(str, ...)    \
    do { \
        const char* strs[] = {__VA_ARGS__,NULL};\
        for(size_t i = 0; strs[i] != NULL; ++i) string_append((str), strs[i]);\
    } while(0)

extern void string_expand(string_t* str, size_t inc);
extern string_t* string_create(const char* data);
extern int string_cmp(string_t* str1, string_t* str2);
extern void string_cpy(string_t* dst, string_t* src);
extern void string_cat(string_t* dst, string_t* src);
extern void string_fappend(string_t* str, const char* fmt, ...);
extern void string_append(string_t* str, const char* data);
extern void string_clear(string_t* str);
extern void string_destroy(string_t* str);

#ifdef STRING_IMPLEMENTATIONS
void string_expand(string_t* str, size_t inc) {
    str->capacity += inc;
    str->data = (char*) string_realloc_memory(str->data, str->size, str->capacity);
    if(str->size == 0) str->data[0] = 0;
}

string_t* string_create(const char* data) {
    string_t* str = (string_t*)malloc(sizeof(string_t));
    str->capacity = 0;
    str->data = NULL;
    str->size = (data ? strlen(data) : 0);

    if(str->size > 0) {
        string_expand(str, str->size + 1);
        memcpy(str->data, data, str->size + 1);
    }

    return str;
}

int string_cmp(string_t* str1, string_t* str2) {
    if(str1->size != str2->size) return (int)((int)str1->size - (int)str2->size);
    return memcmp(str1->data, str2->data, str1->size);
}

void string_cpy(string_t* dst, string_t* src) {
    if(dst->capacity < src->size) string_expand(dst, src->size - dst->capacity);
    memcpy(dst, src, src->size);
    dst->size = src->size;
}

void string_cat(string_t* dst, string_t* src) {
    if((dst->capacity - dst->size) < src->size) string_expand(dst, src->size - (dst->capacity - dst->size));
    memcpy(&dst->data[dst->size], src, src->size);
    dst->size += src->size;
}

void string_fappend(string_t* str, const char* fmt, ...) {
    va_list args, args_copy;
    va_start(args, fmt);

    va_copy(args_copy, args);
    int len = vsnprintf(str->data, 0, fmt, args_copy);
    va_end(args_copy);
    
    if(len > 0) {
        string_expand(str, (len + 1));
        vsnprintf(&str->data[str->size], str->capacity - str->size, fmt, args);
        str->size = str->capacity;
    }
    va_end(args);
}

void string_append(string_t* str, const char* data) {
    if(data == NULL) return;

    if(str->data == NULL) {
        str->capacity = 0;
        str->data = NULL;
        str->size = 0;
    }

    int len = (int)strlen(data);
    int available = (int)str->capacity - (int)str->size - 1;
    if(len >= available) {
        string_expand(str, (len - available));
    }
    strcat(str->data, data);
    str->size += len;
}

void string_clear(string_t* str) {
    if(str->size > 0) {
        memset(str->data, 0, str->size);
        str->size = 0;
    }
}

void string_destroy(string_t* str) {
    if(str && str->data) string_free_memory(str->data);
}
#endif


typedef struct arena_context_t {
    char context[256];
    arena_t arena;
} arena_context_t;

static struct {
    size_t size;
    size_t last;
    arena_context_t* contexts;
} context_stack;

#define TMP_CONTEXT_PUSH()      context_push(__func__)
#define TMP_CONTEXT_POP()       context_pop()
#define TMP_GET_MEMORY(size)    context_get_memory(size)

extern void context_stack_expand();
extern void* context_get_memory(size_t size);
extern int context_push(const char* context_name);
extern int context_pop();

#ifdef CONTEXT_MEMORY_IMPLEMENTATIONS
void context_stack_expand() {
    context_stack.size += 10;
    context_stack.contexts = (arena_context_t*)realloc(context_stack.contexts, sizeof(*context_stack.contexts) * context_stack.size);
    memset(context_stack.contexts, 0, sizeof(*context_stack.contexts) * 10);
}

void* context_get_memory(size_t size) {
    if(context_stack.last == 0) return NULL;
    return arena_get_memory(&context_stack.contexts[context_stack.last - 1].arena, size);
}

int context_push(const char* context_name) {
    size_t len = strlen(context_name);
    if(len >= sizeof(((arena_context_t*)0)->context)) return -1;

    if(context_stack.last == context_stack.size) {
        context_stack_expand();
    }

    arena_context_t* ctx = &context_stack.contexts[context_stack.last];
    if(context_name) memcpy(ctx->context, context_name, len + 1);
    else ctx->context[0] = 0;

#ifdef DEBUG_CONTEXT_ARENA
    printf("ENTER CONTEXT: ");
    for(size_t i = 0; i < context_stack.last; ++i) {
        printf("%s --> ", context_stack.contexts[i].context);
    }
    printf("%s\n", context_name);
#endif

    return context_stack.last++;
}

int context_pop() {
    if(context_stack.last == 0) {
       return -1;
    }
    arena_context_t* ctx = &context_stack.contexts[--context_stack.last];
#ifdef DEBUG_CONTEXT_ARENA
    printf("EXIT CONTEXT: ");
    if(context_stack.last == 0) printf("<-- %s\n", ctx->context);
    else {
        for(size_t i = 0; i < (context_stack.last - 1); ++i) {
            printf("%s --> ", context_stack.contexts[i].context);
        }
        printf("%s <-- %s\n", context_stack.contexts[context_stack.last - 1].context, ctx->context);
    }
#endif
    ctx->context[0] = 0;
#ifndef CONTEXT_ARENA_MEMORY_REUSE
    arena_flush(&ctx->arena);
#else
    arena_reset(&ctx->arena);
#endif
    return context_stack.last;
}
#endif


void* array_realloc_memory(void* addr, size_t old_size, size_t new_size) {
    void* new_addr = TMP_GET_MEMORY(new_size);
    if(addr && old_size) memcpy(new_addr, addr, old_size);
    return new_addr;
}
void array_free_memory(void* addr) {
    (void)addr;
}

void* string_realloc_memory(void* addr, size_t old_size, size_t new_size) {
    void* new_addr = TMP_GET_MEMORY(new_size);
    if(addr && old_size) memcpy(new_addr, addr, old_size);
    return new_addr;
}
void string_free_memory(void* addr) {
    (void)addr;
}

DEFINE_ARRAY_TYPE(args_t, char*)
DEFINE_ARRAY_TYPE(builds_t, pid_t)

typedef struct cmd_t {
    string_t build;
    args_t args;
    args_t paths;
    args_t files;
    args_t extra;
    args_t libs;
}cmd_t;

#define cmd_append_args(cmd, ...) ((cmd) == NULL ? false : cmd_append_data(&(cmd)->args, __VA_ARGS__, NULL))
#define cmd_append_files(cmd, ...) ((cmd) == NULL ? false : cmd_append_data(&(cmd)->files, __VA_ARGS__, NULL))
#define cmd_append_libs(cmd, ...) ((cmd) == NULL ? false : cmd_append_data_with_prefix(&(cmd)->libs, "-l", __VA_ARGS__, NULL))
#define cmd_append_paths(cmd, ...) ((cmd) == NULL ? false : cmd_append_data_with_prefix(&(cmd)->paths, "-I", __VA_ARGS__, NULL))

bool cmd_set_build_tool(cmd_t* cmd, const char* build_tool) {
    if(cmd == NULL || build_tool == NULL) return false;
    string_append(&cmd->build, build_tool);
    return true;
}

bool cmd_append_data(args_t* data, ...) {
    va_list args;
    va_start(args, data);
    
    char* next_arg;
    while ((next_arg = va_arg(args, char*)) != NULL) {
        array_append(data, next_arg);
    }
    
    va_end(args);

    return true;
}

bool cmd_append_data_with_prefix(args_t* data, const char* prefix, ...) {
    va_list args;
    va_start(args, prefix);
    
    char* next_arg;
    while ((next_arg = va_arg(args, char*)) != NULL) {
        string_t str = {0};
        string_append_many(&str, prefix, next_arg);
        array_append(data, str.data);
    }
    
    va_end(args);

    return true;
}


bool build(cmd_t* cmd, const char* out) {
    pid_t pid = fork();

    if(pid < 0) {
        printf("ERROR: Failed to run command\n");
        return false;
    }
    else if(pid == 0) {
        args_t argv = {0};
        array_append(&argv, cmd->build.data);
        array_append_array(&argv, &cmd->args);
        if(out) array_append(&argv, (char*)out);
        array_append_array(&argv, &cmd->files);
        array_append_array(&argv, &cmd->paths);
        array_append_array(&argv, &cmd->extra);
        array_append_array(&argv, &cmd->libs);

        printf("RUN: ");
        for(size_t i = 0; i < argv.size; ++i) {
            printf("%s ", argv.data[i]);
        }
        printf("\n");
        fflush(stdout);

        execvp(argv.data[0], argv.data);
        printf("ERROR: Failed to run command\n");
        return false;
    }
    else {
        int wstatus = 0;
        wait(&wstatus);
        if(!WIFEXITED(wstatus)) return false;
        else return (WEXITSTATUS(wstatus) == 0);
    }
    return true;
}

pid_t build_async(cmd_t* cmd, const char* out) {
    pid_t pid = fork();

    if(pid < 0) {
        printf("ERROR: Failed to run command\n");
        return -1;
    }
    else if(pid == 0) {
        args_t argv = {0};
        array_append(&argv, cmd->build.data);
        array_append_array(&argv, &cmd->args);
        if(out) array_append(&argv, (char*)out);
        array_append_array(&argv, &cmd->files);
        array_append_array(&argv, &cmd->paths);
        array_append_array(&argv, &cmd->extra);
        array_append_array(&argv, &cmd->libs);

        printf("RUN: ");
        for(size_t i = 0; i < argv.size; ++i) {
            printf("%s ", argv.data[i]);
        }
        printf("\n");
        fflush(stdout);

        execvp(argv.data[0], argv.data);
        printf("ERROR: Failed to run command\n");
        return -1;
    }
    return pid;
}

bool builds_wait(builds_t* builds) {
    bool ret = true;
    for(size_t i = 0; i < builds->size; ++i) {
        int status;
        if (waitpid(builds->data[i], &status, 0) == -1) {
            fprintf(stderr, "ERROR: Build command with pid: %d failed", builds->data[i]);
            ret = false;
        }
    }
    return ret;
}

#define for_each_file_at(file, at)  \
    DIR* dir = opendir(at);       \
    struct dirent* entry;           \
    while (dir != NULL && (entry = readdir(dir)) != NULL && (file = entry->d_name))

#define for_each_file_at_clean() if(dir) closedir(dir)

bool is_file_type(const char* file, const char* suffix, size_t suffix_len) {
    size_t file_len = strlen(file);
    if(file_len < suffix_len) return false;
    return (memcmp(&file[file_len - suffix_len], suffix, suffix_len) == 0);
}

bool is_c_file(const char* file) {
    size_t len = strlen(file);
    if(len > 2 && file[len - 1] == 'c' && file[len - 2] == '.') return true;
    return false;
}

void files_append(args_t* args, const char* path, const char* suffix, bool append_to_path) {
    size_t len = ((suffix) ? strlen(suffix) : 0);
    const char* file = NULL;
    for_each_file_at(file, path) {
        if(is_file_type(file, suffix, len)) {
            // Only the pointer will be copied so copy the file name to tmp memory to ensure
            // that the string is still valid when we exit
            // Clean up will be handled outside with a TMP_CONTEXT_POP
            char* name = NULL;
            if(append_to_path) {
                name = TMP_GET_MEMORY(strlen(path) + 1 + strlen(file) + 1);
                strcpy(name, path);
                strcat(name, "/");
                strcat(name, file);
            }
            else {
                name = TMP_GET_MEMORY(strlen(file) + 1);
                strcpy(name, file);
            }
            array_append(args, name);
        }
    } for_each_file_at_clean();
}

bool build_scan_append_dir(cmd_t* cmd, const char* path, bool append) {
    DIR* dir = opendir(path);
    if(dir == NULL) {
        printf("ERROR: Failed to scan folder: %s\n", path);
        return false;
    }
    TMP_CONTEXT_PUSH();
    struct dirent* entry;
    string_t str = {0};
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if(len < 3) continue;
        if(entry->d_name[len - 1] == 'c' && entry->d_name[len - 2] == '.') {
            string_clear(&str);
            if(append) {
                string_append_many(&str, path, "/");
            }
            string_append(&str, entry->d_name);
            cmd_append_files(cmd, str.data, NULL);
        }
    }
    closedir(dir);
    TMP_CONTEXT_POP();

    return true;
}

bool build_add_dir(cmd_t* cmd, const char* path) {
    return build_scan_append_dir(cmd, path, true);
}

bool create_out_dir(const char* path) {
    DIR* dir = opendir(path);
    if(dir == NULL) {
        if(mkdir(path, 0777) != 0) return false;
    }
    else closedir(dir);

    return true;
}

bool delete_file(const char* file) {
    if(unlink(file) == -1) {
        fprintf(stderr, "Error deleting file '%s': %s\n", file, strerror(errno));
        return false;
    }
    return true;
}

bool remove_dir(const char* folder) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    string_t path = {0};

    if((dir = opendir(folder)) == NULL) return false;

    TMP_CONTEXT_PUSH();
    while((entry = readdir(dir)) != NULL) {
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        string_clear(&path);
        string_append_many(&path, folder, "/", entry->d_name);

        if(lstat(path.data, &statbuf) == -1) goto clean_up;

        if(S_ISDIR(statbuf.st_mode)) {
            if(remove_dir(path.data) == false) goto clean_up;
        }
        else if(unlink(path.data) == -1) goto clean_up;
    }
    TMP_CONTEXT_POP();
    closedir(dir);
    return rmdir(folder) == 0;

clean_up:
    TMP_CONTEXT_POP();
    closedir(dir);
    return false;
}

bool build_dir(cmd_t* cmd, const char* path, const char* out) {
    char cwd[256];

    if(getcwd(cwd, sizeof(cwd)) == NULL || chdir(path) != 0) {
        printf("ERROR: Invalida path: %s\n", path);
        return false;
    }

    DIR* dir = opendir(".");
    if(dir == NULL) {
        printf("ERROR: Failed to scan folder: %s\n", path);
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if(len < 3) continue;
        if(entry->d_name[len - 1] == 'c' && entry->d_name[len - 2] == '.') {
            cmd_append_files(cmd, entry->d_name, NULL);
        }
    }
    closedir(dir);

    bool success = build(cmd, out);

    if(chdir(cwd) != 0) {
        printf("Warning: failed to return to working directory: %s\n", cwd);
    }

    return success;
}

bool build_dir_files(cmd_t* cmd, const char* path, const char* out_path) {
    bool ret = true;
    create_out_dir(out_path);
    const char* file = NULL;
    string_t in = {0};
    string_t out = {0};
    TMP_CONTEXT_PUSH();
    for_each_file_at(file, path) {
        if(is_c_file(file)) {
            string_clear(&in);
            string_append_many(&in, path, "/", file);
            string_clear(&out);
            string_append_many(&out, out_path, "/", file);
            out.data[out.size - 1] = 'o';

            array_clear(&cmd->files);
            cmd_append_files(cmd, in.data);
            ret = build(cmd, out.data);
        }
    } for_each_file_at_clean();
    TMP_CONTEXT_POP();
    return ret;
}

int is_file_older(const char* file, const char* ref) {
    struct stat stat1, stat2;
    if(stat(file, &stat1) != 0) return -1;
    if(stat(ref, &stat2) != 0) return -1;

    if (stat1.st_mtime > stat2.st_mtime) {
        return 1;
    }

    return 0;
}

DIR* create_dir_tree(const char* path) {
    ARRAY(char) dir = {0};
    for(size_t i = 0; i < strlen(path) + 1; ++i) {
        if(path[i] == '/' || path[i] == '\0') {
            array_append(&dir, '\0');
            if(create_out_dir(dir.data) != true) {
                printf("ERROR: %s failed to create directory \"%s\"\n", __func__, dir.data);
                return false;
            }
            if(path[i] != '\0') dir.data[dir.size - 1] = '/';
        }
        else array_append(&dir, path[i]);
    }
    return opendir(path);
}

bool copy_file(const char *src_path, const char *dst_path, bool keep_timestamp) {
    FILE *src_file, *dst_file;
    char *buffer;
    size_t fileSize;
    size_t bytesRead;
    struct stat src_stat;
    struct timespec times[2];

    if (stat(src_path, &src_stat) != 0) {
        printf("ERROR: Failed to get \"%s\" stats\n", src_path);
        return false;
    }

    src_file = fopen(src_path, "rb");
    if (src_file == NULL) {
        printf("ERROR: Failed to open \"%s\"\n", src_path);
        return false;
    }

    fseek(src_file, 0, SEEK_END);
    fileSize = ftell(src_file);
    rewind(src_file);

    buffer = TMP_GET_MEMORY(fileSize);

    dst_file = fopen(dst_path, "wb");
    if (dst_file == NULL) {
        printf("ERROR: Failed to open \"%s\"\n", dst_path);
        fclose(src_file);
        return false;
    }

    bytesRead = fread(buffer, 1, fileSize, src_file);
    if (bytesRead == fileSize) {
        fwrite(buffer, 1, fileSize, dst_file);
    } else {
        printf("ERROR: Failed to read from \"%s\"\n", src_path);
    }

    fclose(src_file);
    fclose(dst_file);

    if(keep_timestamp) {
        int fd = open(src_path, O_RDWR, 0644);
        times[0] = src_stat.st_atim;
        times[1] = src_stat.st_mtim;
        if (futimens(fd, times) != 0) {
            printf("WARNING: Failed to set \"%s\" timestamp\n", dst_path);
        }
        close(fd);
    }

    return (bytesRead != fileSize);
}

bool fetch_local_files(const char* src, const char* dst, args_t* files) {
    DIR* src_dir = opendir(src);
    if(src_dir == NULL) {
        printf("ERROR: %s source directory \"%s\" doesn't exist\n", __func__, src);
        return false;
    }
    DIR* dst_dir = create_dir_tree(dst);
    if(dst_dir == NULL) {
        printf("ERROR: %s failed to create destination directory \"%s\"\n", __func__, dst);
        closedir(src_dir);
        return false;
    }

    ARRAY(string_t) files_to_check = {0};
    struct dirent* entry;
    while ((entry = readdir(src_dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            if(entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')) {
                continue;
            }
        }
        if(files != NULL) {
            for(size_t i = 0; i < files->size; ++i) {
                if(strcmp(files->data[i], entry->d_name) == 0){
                    string_t str = {0};
                    string_append(&str, entry->d_name);
                    array_append(&files_to_check, str);
                }
            }
        }
        else {
            string_t str = {0};
            string_append(&str, entry->d_name);
            array_append(&files_to_check, str);
        }
    }
    closedir(src_dir);

    ARRAY(string_t) dst_files = {0};
    while ((entry = readdir(dst_dir)) != NULL) {
        if(entry->d_name[0] == '.') {
            if(entry->d_name[1] == '\0' || (entry->d_name[1] == '.' && entry->d_name[2] == '\0')) {
                continue;
            }
        }
        string_t str = {0};
        string_append(&str, entry->d_name);
        array_append(&dst_files, str);
    }
    closedir(dst_dir);

    bool copy = true;
    string_t src_file = {0};
    string_t dst_file = {0};
    for(size_t i = 0; i < files_to_check.size; ++i) {
        for(size_t j = 0; j < dst_files.size; ++j) {
            if(strcmp(files_to_check.data[i].data, dst_files.data[j].data) == 0) {
                string_clear(&src_file);
                string_append(&src_file, src);
                string_append(&src_file, "/");
                string_append(&src_file, files_to_check.data[i].data);
                string_clear(&dst_file);
                string_append(&dst_file, dst);
                string_append(&dst_file, "/");
                string_append(&dst_file, dst_files.data[j].data);
                if(is_file_older(src_file.data, dst_file.data) == 1) {
                    break;
                }
                else {
                    copy = false;
                    break;
                }
            }
        }
        if(copy) {
            if(src_file.size == 0) {
                string_append(&src_file, src);
                string_append(&src_file, "/");
                string_append(&src_file, files_to_check.data[i].data);
            }
            if(dst_file.size == 0) {
                string_append(&dst_file, dst);
                string_append(&dst_file, "/");
                string_append(&dst_file, files_to_check.data[i].data);
            }
            printf("INFO: Copy file: \"%s\" to \"%s\"\n", src_file.data, dst_file.data);
            copy_file(src_file.data, dst_file.data, true);
            string_clear(&src_file);
            string_clear(&dst_file);
        }
        else copy = true;
    }

    return true;
}

void rebuild_c_builder(int argc, char* argv[]) {
    if(is_file_older("cb.h", argv[0]) != 1 &&
       is_file_older("cb.c", argv[0]) != 1) return;

    TMP_CONTEXT_PUSH();
    cmd_t cmd = {0};
    cmd_set_build_tool(&cmd, "gcc");
    (void)cmd_append_args(&cmd, "-O3", "-Wall", "-o");
    (void)cmd_append_files(&cmd, "cb.c");
    if(build(&cmd, "cb") == true) {
        (void)execvp(argv[0], argv);
    }
    TMP_CONTEXT_POP();
    exit(1);
}

bool run_exec(const char* exec, int argc, char* argv[]) {
    TMP_CONTEXT_PUSH();

    args_t args = {0};
    array_append(&args, (char*)exec);
    array_append_carray(&args, argv, argc);

    printf("START: ");
    for(size_t i = 0; i < args.size; ++i) {
        printf("%s ", args.data[i]);
    }
    printf("\n");
    fflush(stdout);

    execvp(args.data[0], args.data);
    fprintf(stderr, "ERROR: Failed to run %s\n", exec);
    return false;
}

#endif