#include "coin.h"
#include "csv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum {
    ID_FIELD = 0,
    DATE_FIELD = 1,
    TYPE_FIELD = 2,
    COIN_FIELD = 3,
    AMOUNT_FIELD = 4,
    USD_FIELD = 5,
};

int Coin_operationType(const char* str, size_t len) {
    switch(len + 1) {
    case sizeof("Buy"):
        if(strcmp("Buy", str) == 0) return COIN_BUY;
        break;
    case sizeof("Sell"):
        if(strcmp("Sell", str) == 0) return COIN_SELL;
        break;
    case sizeof("Yield"):
        if(strcmp("Yield", str) == 0) return COIN_YIELD;
        break;
    default: return -1;
    }
    return 0;
}

char* strupper(char* dst, const char* src) {
    char* ret = dst;
    while(*src) *dst++ = toupper(*src++);
    *dst = 0;
    return ret;
}

char* Coin_getInfo(const char* name, const char* csvFile) {
    csv_raw_t csv;
    CSV_load(&csv, csvFile);

    char ticker[32];
    strupper(ticker, name);

    float amount_coin = 0;
    float totalBuy_usd = 0;
    float totalBuy_coin = 0;
    float totalSell_usd = 0;
    const char* line = NULL;
    for(int i = 0; (line = CSV_getLine(&csv, i)) != NULL; ++i){
        char tmp[16];

        CSV_getField(line, AMOUNT_FIELD, tmp, sizeof(tmp));
        double amount = strtod(tmp, NULL);

        CSV_getField(line, USD_FIELD, tmp, sizeof(tmp));
        double price = strtod(tmp, NULL);

        size_t size = CSV_getField(line, TYPE_FIELD, tmp, sizeof(tmp));
        switch(Coin_operationType(tmp, size)) {
        case COIN_BUY:
            amount_coin += amount;
            totalBuy_usd += price;
            totalBuy_coin += amount;
            break;
        case COIN_SELL:
            amount_coin -= amount;
            totalSell_usd += price;
            break;
        case COIN_YIELD:
            amount_coin += amount;
            totalBuy_usd += 0;
            totalBuy_coin += amount;
            break;
        default:
            printf("ERROR: Bad transaction: %s\n", line);
            break;
        }
    }
    float avgPrice_usd = totalBuy_usd / totalBuy_coin;

    CSV_close(&csv);

    const char format[] = "{\"symbol\":\"%s\",\"amount\":%f,\"avgPrice\":%f,\"totalBuy\":%f,\"totalSell\":%f}";
    char* coinJson = malloc(sizeof(format) + 10 + 10 * 4);
    sprintf(coinJson, format, ticker, amount_coin, avgPrice_usd, totalBuy_usd, totalSell_usd);
    
    return coinJson;
}

char* Coin_getTransactions(const char* name, const char* csvFile) {
    csv_raw_t csv;
    CSV_load(&csv, csvFile);
    
    char ticker[32];
    strupper(ticker, name);

    const char format[] = "{\"coin\":\"%s\",\"amount\":%f,\"value\":%f,\"date\":\"%s\"},";
    char* transactionsJson = malloc(csv.entries * (sizeof(format) + 128));
    const char* line = NULL;
    transactionsJson[0] = '[';
    transactionsJson[1] = 0;
    size_t len = 1;
    for(int i = 0; (line = CSV_getLine(&csv, i)) != NULL; ++i) {
        char tmp[32];

        CSV_getField(line, AMOUNT_FIELD, tmp, sizeof(tmp));
        double amount_coin = strtod(tmp, NULL);

        CSV_getField(line, USD_FIELD, tmp, sizeof(tmp));
        double value_usd = strtod(tmp, NULL);

        CSV_getField(line, DATE_FIELD, tmp, sizeof(tmp));

        len += sprintf(transactionsJson + len, format, ticker, amount_coin, value_usd, tmp);
    }
    transactionsJson[len - 1] = ']';
    transactionsJson[len] = 0;

    CSV_close(&csv);

    return transactionsJson;
}

int Coin_addTransacation(const char* name, const char* csvFile, const char* transactionJson) {
    csv_raw_t csv;
    if(CSV_load(&csv, csvFile) == -1) {
        CSV_create(&csv, csvFile);
    }

    char ticker[32];
    strupper(ticker, name);
    
    char* str = strstr(transactionJson, "amount");
    str += sizeof("amount\":") - 1;
    double amount = strtod(str, &str);

    str = strstr(str, "type");
    str += sizeof("type\":\"") - 1;
    char type[8];
    int i = 0;
    for(; *str != '\"'; i++) type[i] = *str++;
    type[i] = 0;

    str = strstr(str, "value");
    str += sizeof("value\":") - 1;
    double price = strtod(str, &str);

    str = strstr(str, "date");
    str += sizeof("date\":\"") - 1;
    char date[32];
    for(i = 0; *str != '\"'; i++) date[i] = *str++;
    date[i] = 0;

    char transaction[256];
    sprintf(transaction, "%s;%s;%s;%f;%f", date, type, ticker, amount, price);
    printf("TRANSACTION: %s\n\n", transaction);
    CSV_appendNewLine(&csv, transaction);

    int id = csv.lastId - 1;

    CSV_close(&csv);

    return id;
}