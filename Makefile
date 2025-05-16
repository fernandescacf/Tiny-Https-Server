CFLAGS = -g -w -O2
CC = gcc
LD = ld
AR = ar
LIBS = -lpthread -lssl -luuid
OBJS = build/objs
OUT_LIBS = build/libs
EXEC = build

all: prepare main server http_session async
	$(CC) $(OBJS)/main.o $(OBJS)/portfolio.o $(OBJS)/coin.o $(OBJS)/csv.o \
	$(OBJS)/http_session.o $(OUT_LIBS)/async.a $(OUT_LIBS)/server.a -o $(EXEC)/web_app $(LIBS)

prepare:
	mkdir -p $(EXEC)
	mkdir -p $(OBJS)
	mkdir -p $(OUT_LIBS)

main:
	$(CC) $(CFLAGS) -c app/app.c -Iserver -Ihttp_libs -o $(OBJS)/main.o
	$(CC) $(CFLAGS) -c app/backend/portfolio.c -o $(OBJS)/portfolio.o
	$(CC) $(CFLAGS) -c app/backend/coin.c -o $(OBJS)/coin.o
	$(CC) $(CFLAGS) -c app/backend/csv.c -o $(OBJS)/csv.o

http_session:
	$(CC) $(CFLAGS) -c http_libs/http_session.c -Iserver -Ilibs -o $(OBJS)/http_session.o

server: prepare
	$(CC) $(CFLAGS) -c server/server.c -Ilibs -o $(OBJS)/server.o $(LIBS)
	$(CC) $(CFLAGS) -c server/http.c -Ilibs -o $(OBJS)/http.o $(LIBS)
	$(AR) rcs $(OUT_LIBS)/server.a $(OBJS)/server.o $(OBJS)/http.o

async: prepare
	$(CC) $(CFLAGS) -c libs/threadpool.c -Ilibs -o $(OBJS)/threadpool.o $(LIBS)
	$(CC) $(CFLAGS) -c libs/queue.c -Ilibs -o $(OBJS)/queue.o $(LIBS)
	$(CC) $(CFLAGS) -c libs/async.c -Ilibs -o $(OBJS)/async.o $(LIBS)
	$(AR) rcs $(OUT_LIBS)/async.a $(OBJS)/threadpool.o $(OBJS)/queue.o $(OBJS)/async.o


clean:
	rm -rf $(OBJS)
	rm -rf $(OUT_LIBS)
	rm -rf $(EXEC)

PHONY: server async clean