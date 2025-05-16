#ifndef _PORTFOLIO_H_
#define _PORTFOLIO_H_

int PortfolioCreate(const char* path, const char* name);

char* PortfolioGetCoinsList(const char* portfolio);

char* PortfolioGetCoinTransactions(const char* portfolio, const char* coin);

char* PortfolioAddCoinTransactions(const char* portfolio, const char* coin, const char* transactionJson);

#endif