#ifndef _COIN_H_
#define _COIN_H_

#include <stdbool.h>

enum {
    COIN_BUY = 0,
    COIN_SELL = 1,
    COIN_YIELD = 2,
};

char* Coin_getInfo(const char* name, const char* csvFile);

char* Coin_getTransactions(const char* name, const char* csvFile);

int Coin_addTransacation(const char* name, const char* csvFile, const char* transactionJson);

#endif