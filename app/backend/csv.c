#include "csv.h"
#include <string.h>

#define CSV_DEFUALT_LINES   10

int CSV_create(csv_raw_t* csv, const char* path) {
    csv->fd = fopen(path, "w");
    csv->lastId = 0;
    csv->size = CSV_DEFUALT_LINES;
    csv->entries = 0;
    csv->lines = calloc(csv->size, sizeof(*csv->lines));
    csv->hdr_size = sizeof("Id;Date;Type;Coin;Amount;Usd\n") - 1;
    fprintf(csv->fd, "Id;Date;Type;Coin;Amount;Usd\n");
    fflush(csv->fd);
    fclose(csv->fd);
    csv->fd = fopen(path, "r+");
    return 0;
}

void CSV_close(csv_raw_t* csv) {
    for(int i = 0; i < csv->size; ++i) {
        free(csv->lines[i]);
        csv->lines[i] = NULL;
    }
    free(csv->lines);
    fclose(csv->fd);
    memset(csv, 0, sizeof(*csv));
}

int CSV_load(csv_raw_t* csv, const char* path) {
    if((csv->fd = fopen(path, "r+")) == NULL) return -1;
    csv->lastId = 0;
    csv->size = CSV_DEFUALT_LINES;
    csv->entries = 0;
    csv->lines = calloc(csv->size, sizeof(*csv->lines));

    size_t len = 0;
    // Skip first line
    csv->hdr_size = getline(&csv->lines[0], &len, csv->fd);
    // Get all entries
    while(getline(&csv->lines[csv->entries], &len, csv->fd) > 0) {
        csv->entries += 1;
        if(csv->entries == csv->size) {
            csv->lines = realloc(csv->lines, sizeof(*csv->lines) * (csv->size += CSV_DEFUALT_LINES));
            memset(&csv->lines[csv->entries], 0, CSV_DEFUALT_LINES * sizeof(char*));
        }
    }
    if(csv->entries > 0) csv->lastId = atoi(csv->lines[csv->entries - 1]);
    return 0;
}

int CSV_removeLineById(csv_raw_t* csv, int id) {
    if(id > atoi(csv->lines[csv->entries - 1])) return -1;

    int entry = ((id >= csv->entries) ? (csv->entries - 1) : (id - 1));
    for(; entry >= 0 && id != atoi(csv->lines[entry]); --entry);

    if(entry < 0) return -1;

    csv->entries -= 1;
    size_t bytes_to_remove = strlen(csv->lines[entry]);
    free(csv->lines[entry]);
    if(entry < csv->entries){
        memmove(&csv->lines[entry], &csv->lines[entry + 1], sizeof(*csv->lines) * (csv->entries - entry));
    }

    fseek(csv->fd , 0L, SEEK_END);
    size_t file_size = ftell(csv->fd);

    if(entry < csv->entries){
        size_t skip_bytes = csv->hdr_size;
        for(int i = 0; i < entry; ++i) {
            skip_bytes += strlen(csv->lines[i]);
        }

        fseek(csv->fd, skip_bytes, SEEK_SET);

        for(int i = entry; i < csv->entries; ++i) {
            fputs(csv->lines[i], csv->fd);
        }
    }

    if(ftruncate(fileno(csv->fd), file_size - bytes_to_remove) != 0) return -1;

    fflush(csv->fd);

    return 0;
}

int CSV_appendNewLine(csv_raw_t* csv, const char* line) {
    if(csv->entries == csv->size) {
        csv->lines = realloc(csv->lines, sizeof(*csv->lines) * (csv->size += CSV_DEFUALT_LINES));
        memset(&csv->lines[csv->entries], 0, CSV_DEFUALT_LINES * sizeof(char*));
    }
    char *str = malloc(sizeof("1234567890;") + strlen(line));
    sprintf(str, "%d;%s\n", ++csv->lastId, line);
    csv->lines[csv->entries++] = str;

    fseek(csv->fd , 0L, SEEK_END);
    fprintf(csv->fd, "%s", str);
    fflush(csv->fd);

    return 0;
}

void CSV_print(csv_raw_t* csv) {
    for(int i = 0; i < csv->entries; ++i) {
        printf("%s", csv->lines[i]);
    }
}

const char* CSV_getLine(csv_raw_t* csv, int line) {
    if(line < csv->entries) {
        return csv->lines[line];
    }
    return NULL;
}

const char* CSV_getLineById(csv_raw_t* csv, int id) {
    if(id > atoi(csv->lines[csv->entries - 1])) return NULL;

    int entry = ((id >= csv->entries) ? (csv->entries - 1) : (id - 1));
    for(; entry >= 0 && id != atoi(csv->lines[entry]); --entry);

    if(entry < 0) return NULL;

    return csv->lines[entry];
}

int CSV_getField(const char* line, int num, char* field, size_t size) {

    for(int i = 0; line[i] != '\n'; ++i) {
        if(num == 0 || (line[i] == ';' && --num == 0)) {
            int j = ((i == 0) ? (0) : (i + 1));
            while(line[j] != ';' && line[j] != '\n' && --size > 0) {
                *field++ = line[j++];
            }
            *field = 0;
            if(size) return (j - i - 1);
            else return -1;
        }
    }
    return -1;
}