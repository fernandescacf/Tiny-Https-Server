#include "portfolio.h"
#include "coin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

DIR* OpenDirectory(const char* dir_path) {
    DIR* dir = opendir(dir_path);
    if(dir == NULL) {
        if(errno == ENOENT && mkdir(dir_path, 0777) == 0) {
            return opendir(dir_path);
        }
        else return NULL;
    }
    return dir;
}

void CloseDirecotry(DIR* dir) {
    closedir(dir);
}

int PortfolioCreate(const char* path, const char* name) {
    // Replace any space with _
    size_t name_len = strlen(name);
    size_t path_len = strlen(path);
    char new_name[name_len + 1 + path_len];
    
    memcpy(new_name, path, path_len);
    for(size_t i = 0; i < name_len ; ++i) {
        new_name[path_len + i] = (name[i] == ' ' ? '_' : name[i]);
    }
    new_name[name_len + path_len] = 0;

    DIR* dir = OpenDirectory(new_name);
    if(dir == NULL) return -1;
    CloseDirecotry(dir);
    return 0;
}

size_t PortfolioCountCoins(DIR* portfolioDir) {
    struct dirent* entry;
    size_t count = 0;

    while ((entry = readdir(portfolioDir)) != NULL) {
        if (entry->d_type == DT_REG) count++;
    }
    rewinddir(portfolioDir);
    return count;
}

char* PortfolioGetCoinsList(const char* portfolio) {
    char* coinsJson = malloc(4096*2);
    struct dirent* entry;
    struct stat buf;
    char file[256] = {0};
    DIR* dir = OpenDirectory(portfolio);
    if(dir == NULL) return NULL;

    strcpy(file, portfolio);
    size_t len = strlen(file);
    file[len++] = '/';

    coinsJson[0] = '[';
    size_t size = 1;
    while ((entry = readdir(dir)) != NULL) {
        sprintf(&file[len], "%s", entry->d_name);
        stat(file, &buf);

        if(S_ISREG(buf.st_mode)) {
            char name[16];
            int i;
            for(i = 0; entry->d_name[i] != '.'; ++i) name[i] = entry->d_name[i];
            name[i] = '\0';
            
            char* coin = Coin_getInfo(name, file);
            size += sprintf(&coinsJson[size], "%s,", coin);
            free(coin);
        }
    }
    coinsJson[size - 1] = ']';

    CloseDirecotry(dir);

    return coinsJson;
}

char* strlower(char* dst, const char* src) {
    char* ret = dst;
    while(*src) *dst++ = tolower(*src++);
    *dst = 0;
    return ret;
}

char* PortfolioGetCoinTransactions(const char* portfolio, const char* coin) {
    char csv_file[256] = {0};
    char ticker[32] = {0};
    sprintf(csv_file, "%s/%s.csv", portfolio, strlower(ticker, coin));
    return Coin_getTransactions(coin, csv_file);
}

char* PortfolioAddCoinTransactions(const char* portfolio, const char* coin, const char* transactionJson) {
    char csv_file[256] = {0};
    char ticker[32] = {0};

    char* str = strstr(transactionJson, "coin");
    str += sizeof("coin\":\"") - 1;
    int i = 0;
    for(i = 0; *str != '\"'; i++) ticker[i] = tolower(*str++);
    ticker[i] = 0;

    sprintf(csv_file, "%s/%s.csv", portfolio, strlower(ticker, ticker));
    Coin_addTransacation(ticker, csv_file, transactionJson);

    return (char*)transactionJson;
}