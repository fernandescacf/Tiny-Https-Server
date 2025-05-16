# My Tiny Https Server
Simple https server implemented in C as an exploration project to understand how http works.

It was only tested in Linux and compiled using gcc.

## server
Contains the https server implementation.

Include http.h to have access to all interfaces available to use the https server.

The program main() is located in server.c and the application must use app_start(...) and app_stop() to put the application initialization and termination code.

## libs
Contains libraries that provide the multithread functionalities used by the server.

## http_libs
Contains support libraries to be used by the application.

Currently only provides a simple library to handle sessions, that was implemented for exploration and should not be used in production.

## Application example
Simple crypto portfolio tracker.

## How to build app example
1. Use boring Makefile: ````make````
2. Use the exciting C Builder tool:
2. Use the exciting C Builder tool:
   * Bootstrap C Builder: ````gcc -o cb cb.c````
   * ````./cb````

## How to run app example
The server expects a port to be provided using '-p', a full chain certificate using '--pem' and a private key using '--key'.

Full list of arguments:
* <b>'--port'/'-p':</b> Port number to be used by the server.
* <b>'--pem':</b> Full chain certificate.
* <b>'--key':</b> Private key
* <b>'--ip':</b> Ip to be used by the server.
* <b>'--connections'/'-c':</b> Number of parallel connections allowed
* <b>'--tasks'/'-t':</b> Max number of parallel tasks
* <b>'--help'/-h':</b> Prints help menu

It is also possible to pass arguments to the application using '--'.
### Example:
````bash
./build/bin/app -p 7777 -t 6 -c 20 --key "certificates/server.key" --pem "certificates/cert-chain.pem" -- --root "app" --timeout 12h
````

## How to generate private key and full chain certificate locally
````bash
openssl genrsa -out ca.key 2048
openssl req -x509 -new -key ca.key -days 365 -out ca.crt
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 365 -out server.crt
cat server.crt ca.crt > cert-chain.pem
````

## Future Ideas (must likely will never be implemented)
* Add support to load application from dynamic libraries, we could even have hot reloading.
* Improve http_session by adding encryption to the passwords.
* Add library to generate html files.
* Improve https server (add functionalities and bug fixing).