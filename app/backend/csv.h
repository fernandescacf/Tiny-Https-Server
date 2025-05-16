#ifndef _CSV_H_
#define _CSV_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

typedef struct csv_raw_t {
    FILE*  fd;
    int    lastId;
    size_t hdr_size;
    size_t size;
    size_t entries;
    char** lines;
}csv_raw_t;

int CSV_create(csv_raw_t* csv, const char* path);

void CSV_close(csv_raw_t* csv);

int CSV_load(csv_raw_t* csv, const char* path) ;

int CSV_removeLineById(csv_raw_t* csv, int id);

int CSV_appendNewLine(csv_raw_t* csv, const char* line);

void CSV_print(csv_raw_t* csv);

const char* CSV_getLine(csv_raw_t* csv, int line);

const char* CSV_getLineById(csv_raw_t* csv, int id);

int CSV_getField(const char* line, int num, char* field, size_t size);

#endif