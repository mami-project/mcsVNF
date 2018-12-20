#ifndef _common_h
#define _common_h

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#define CA_LIST "root.pem"
#define HOST	"localhost"
#define RANDOM  "random.pem"
#define PORT	4433
#define BUFSIZZ 20000
#define BUFTLS 16384 

extern BIO *bio_err;
int berr_exit (char *string);
int err_exit(char *string);

SSL_CTX *initialize_ctx(char *keyfile, char *password, char *protocol);
void destroy_ctx(SSL_CTX *ctx);
void set_nagle(int sock, int flag);
int TokenizeString(char *s_String, char ***s_Token, int *size, char c_Delimiter);

typedef struct experiment_info {
	int num_slices;
	int num_proxies;
	int file_size;
	int app_bytes_read;
	int app_bytes_written;
} ExperimentInfo;

#ifndef ALLOW_OLD_VERSIONS
#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
#error "Must use OpenSSL 0.9.6 or later"
#endif
#endif

#endif


