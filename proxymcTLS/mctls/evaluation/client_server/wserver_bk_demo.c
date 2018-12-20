/* 
 * Copyright (C) Telefonica 2015
 * All rights reserved.
 *
 * Telefonica Proprietary Information.
 *
 * Contains proprietary/trade secret information which is the property of 
 * Telefonica and must not be made available to, or copied or used by
 * anyone outside Telefonica without its written authorization.
 *
 * Authors: 
 *   Matteo Varvello <matteo.varvello@telefonica.com> et al. 
 *
 * Description: 
 * An SSL/SPP server. 
 */


#include "common.h"
#include <stdbool.h>            
#include <time.h>
#define KEYFILE "server.pem"
#define PASSWORD "password"
#define DHFILE "dh1024.pem"
#include <openssl/e_os2.h>
#define DEBUG
#define MAX_PACKET 16384 
#define HI_DEF_TIMER


static char *strategy = "uni";
static int disable_nagle = 0; //default is disabled

// Listen TCP socket
int tcp_listen(){
    
	int sock;
	struct sockaddr_in sin;
	int val = 1;

	// Create socket, allocate memory and set sock options
	if((sock=socket(AF_INET,SOCK_STREAM,0)) < 0){
		err_exit("Couldn't make socket");
	}
    memset(&sin, 0, sizeof(sin));
    sin.sin_addr.s_addr = INADDR_ANY;
    //sin.sin_addr.s_addr = inet_addr("192.168.122.226");
    sin.sin_family = AF_INET;
    sin.sin_port = htons(PORT);   //#defina PORT; variable definida en el common.h
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val,sizeof(val));

    if (disable_nagle == 1)
    	set_nagle(sock, 1); 

	// Bind to socket    
	if(bind(sock,(struct sockaddr *)&sin, sizeof(sin))<0){
		berr_exit("Couldn't bind");
	}

	// Listen to socket
    listen(sock,5);  

	// Return socket descriptor
    return(sock);
}

// TCP connect function 
int tcp_connect(char *host, int port){

	struct hostent *hp;
	struct sockaddr_in addr;
	int sock;

	// Resolve host 
	if(!(hp = gethostbyname(host))){
		berr_exit("Couldn't resolve host");
	}
	#ifdef DEBUG
	printf("Host %s resolved to %s port %d\n",  host,  hp->h_addr_list[0], port);
	#endif



	memset(&addr, 0, sizeof(addr));
	addr.sin_addr = *(struct in_addr*)
	hp->h_addr_list[0];
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	if((sock=socket(AF_INET,SOCK_STREAM, IPPROTO_TCP))<0){
		err_exit("Couldn't create socket");
	}
	#ifdef DEBUG
	printf("Socket created\n"); 
	#endif

    if (disable_nagle == 1)
    	set_nagle(sock, 1); 

	if(connect(sock,(struct sockaddr *)&addr, sizeof(addr))<0){
		err_exit("Couldn't connect socket");
	}
	#ifdef DEBUG
	printf("Socket connected\n"); 
	#endif
	
	return sock;
}


// Load parameters from "dh1024.pem"
void load_dh_params(SSL_CTX *ctx, char *file){
	DH *ret=0;
	BIO *bio;

    if ((bio=BIO_new_file(file,"r")) == NULL){
      berr_exit("Couldn't open DH file");     //lee dh1024.pem "r", el BIO_new_file por si solo no puede leer datos
                                              //con extensión .pem
	}

	ret = PEM_read_bio_DHparams(bio, NULL, NULL, NULL);  //lee valores de dh1024.pen
	BIO_free(bio);  //libera bio
	if(SSL_CTX_set_tmp_dh(ctx,ret) < 0){    //establece parametros DH para ser usados por ret, si es 0 es error, si es 1 es exitoso
		berr_exit("Couldn't set DH parameters");
	}
}

// Generate ephemeral RSA key (check DH) 
void generate_eph_rsa_key(ctx)
  SSL_CTX *ctx;
  {
    RSA *rsa;

    rsa = RSA_generate_key(512, RSA_F4, NULL, NULL);
    
    if (!SSL_CTX_set_tmp_rsa(ctx,rsa))
      berr_exit("Couldn't set RSA key");

    RSA_free(rsa);
  }
    
// Compute the size of a file to be served
int calculate_file_size(char *filename){ 

	FILE *fp; 	
	int sz = 0; 

	// Open file 
 	fp = fopen(filename,"r");
 
	// Check for errors while opening file
	if(fp == NULL){
		printf("Error while opening file %s.\r\n", filename);
		exit(-1);
	}
	
	// Seek  to the end of the file and ask for position 
	fseek(fp, 0L, SEEK_END);
	sz = ftell(fp);
	//fseek(fp, 0L, SEEK_SET);

	// Close file 
	fclose (fp);
	
	// Return file size 
	#ifdef DEBUG
	printf ("[DEBUG] File requested is <<%s>> with size <<%d bytes>>\n", filename, sz); 
	#endif 
	return sz; 
}

// Function to print a prox list
void print_proxy_list(SPP_PROXY **proxies, int N){
	int i; 

	#ifdef DEBUG
	printf("[DEBUG] Print proxy list (There are %d available proxies)\r\n", N);
	#endif

	// Iterate through list 
	for (i = 0; i < N; i++){
		printf("Proxy %d -- %s\r\n", i, proxies[i]->address);
	}
}


// Check for SSL_write error (just write at this point) -- TO DO: check behavior per slice 
void check_SSL_write_error(SSL *ssl, int r, int request_len){

    switch(SSL_get_error(ssl, r)){
        case SSL_ERROR_NONE:
            if(request_len != r){
                err_exit("Incomplete write!");
            }
            break;

        default:
            berr_exit("SSL write problem");
    }
}

// splitting 
void splitting (SSL *ssl, char *request, int request_len){
	int beginIndex = 0, i, inc, usedSlices; 

	// For client/server strategy just use half slices 	
	if (strcmp(strategy, "cs") == 0){
		 usedSlices = ssl->slices_len / 2; 
	} else{
		 usedSlices = ssl->slices_len;
	}

	// Compute increment 
	inc = request_len / (usedSlices); 
	
	// Slicing happens here  
	for (i = 0; i < usedSlices; i++){
		
		char* dest = (char*) malloc(inc);  //inc tiene el contenido del index.html
		#ifdef DEBUG
		printf("[DEBUG] Write sub-record with slice [%d ; %s]. (strategy <<%s>>)\n"\
			"[DEBUG] Sub-record size is %d (beginIndex=%d -- endIndex=%d)\n", \
			ssl->slices[i]->slice_id, ssl->slices[i]->purpose, strategy, inc, beginIndex, (beginIndex + inc)); 
		#endif
    	memset(dest, 0, inc);  //copia el contenido de inc a dest, inc tiene la info de index.html
		memcpy(dest, request + beginIndex,  inc);  //copia lo que hay en request + deginIndex a dest y le
		                                           //tienes que poner la logitud que en este caso es inc
		#ifdef DEBUG
		printf("%s\n", dest);  //aqui imprime los slites
		#endif 
		int r = SPP_write_record(ssl, dest, inc, ssl->slices[i]);
		#ifdef DEBUG
		printf("Wrote %d bytes\n", r);
		#endif
		check_SSL_write_error(ssl, r, inc);
		
		// Move pointers
		beginIndex += inc;

		// Increase pointer for last slice  
		if ( i == (usedSlices - 2)){
			inc = request_len - beginIndex; 
		} 
	
		// free memory 
		free (dest); 
	}
}

// Send some data (SSL and SPP) 
void sendData(SSL *ssl, int s, char *request, char *proto, int request_len){

	int r; 

	//mmmm  
	//request_len--; 

	// logging
	#ifdef DEBUG
	printf("[DEBUG] sendData with length %d\n", request_len); 
	#endif 
	
	// SPP
	if (strcmp(proto, "spp") == 0){
		// TO DO implement further splitting here 
		if (request_len < ssl->slices_len){
			r = SPP_write_record(ssl, request, request_len, ssl->slices[0]);
			#ifdef DEBUG
			printf("Wrote %d bytes\n", r);
			#endif
			check_SSL_write_error(ssl, r, request_len);
		}else{
			splitting(ssl, request, request_len); 
		}
	}	
	// SSL
	else if (strcmp(proto, "ssl") == 0){
		r = SSL_write(ssl, request, request_len); 
		check_SSL_write_error(ssl, r, request_len);
	}
	else if (strcmp(proto, "pln") == 0){
		r = write(s, request, request_len); 
	}

}

// Simple test for SPP handshake 
static int http_simple_response(SSL *ssl, int s, char *proto){

	int i; 

	#ifdef DEBUG
	printf("[DEBUG] HTTP test simple response\n"); 
	#endif 	
	
	#ifdef DEBUG
	printf("[DEBUG] Verify proxies and slices were correctly received\n"); 

	if (strcmp(proto, "spp") == 0){
		// Print proxies
		for (i = 0; i < ssl->proxies_len; i++){
			printf("\t[DEBUG] Proxy: %s\n", ssl->proxies[i]->address); 
		}

		// print slices
		for (i = 0; i < ssl->slices_len; i++){
			printf("\t[DEBUG] Slice with ID %d and purpose %s\n", ssl->slices[i]->slice_id, ssl->slices[i]->purpose); 
		}
	}
	#endif 

	// put 200 OK on the wire 
	char *data = "HTTP/1.0 200 OK\r\n"; 
	int request_len = strlen(data);
	sendData(ssl, s,  data, proto, request_len); 

	if (strcmp(proto, "pln") != 0)
	{
		// Shutdown SSL - TO DO (check what happens here) 
		int r = SSL_shutdown(ssl);
		if( !r ){
			shutdown(s, 1);
			r = SSL_shutdown(ssl);
	    }

		// Verify that shutdown was good 
		switch(r){  
			case 1:
	       		break; // Success
			case 0:
			case -1:
			default: // Error 
				#ifdef DEBUG 
				printf ("Shutdown failed with code %d\n", r); 
				#endif 
				berr_exit("Shutdown failed");
		}
		// free SSL 
	    SSL_free(ssl);
	}
	
	// All good	
	return 0; 
}


// Parse HTTP GET request to get filename 
char* parse_http_get(char *buf){ 

	char *delim="\r\n"; 
	char *token = strtok(buf , delim);  //me devuelve el contenido del buf hasta delim y lo guarda en token
	delim=" /"; 
	char *fn = strtok(token, delim);   
	fn = strtok(NULL, delim);
	return fn; 
}


// Serve some data in browser mode, just for SPP protocol 
int serveDataSPPBrowser(SSL *ssl, int s,  int data_size, char *proto, long *resp_len_arr, int arr_len){ 
	
	int i; 
	for (i = 0; i < ssl->slices_len; i++){
		long still_to_send = 0;
		if (i >= arr_len) {
			printf("Error! no data specified to send for slice.\n");
		} else {
			still_to_send = resp_len_arr[i]; 
		}
		if (still_to_send > 0){
			#ifdef DEBUG
			printf ("[DEBUG] %d bytes to be sent with slice %d\n", still_to_send, i); 
			#endif 
			for (;;){

				// Derive min between how much to send and max buffer size 
				long toSend = BUFTLS; 
				if (still_to_send < toSend){
					toSend = still_to_send; 
				}

				//Allocate buffer with size to send and fill with "!"
				#ifdef DEBUG
				printf ("[DEBUG] Allocating buffer with %d data\n", toSend); 
				#endif 
				char *buf = (char*) malloc(sizeof(char) * toSend);
				memset(buf, '!', sizeof(char) * toSend);
				
				#ifdef VERBOSE
//				printf ("[DEBUG] Buffer=\n%s\n", buf); 
				#endif 
			
				// send data on slice i 
				int r = SPP_write_record(ssl, buf, toSend, ssl->slices[i]);
				#ifdef DEBUG
				printf("Wrote %d bytes\n", r);
				#endif
				check_SSL_write_error(ssl, r, toSend);
			
				// Update how much content is still to send 
				still_to_send -= toSend;
				if (still_to_send == 0){
					#ifdef DEBUG
					printf ("[DEBUG] No more data to send for slice %d\n", i); 
					#endif 
					break; 	
				}

				// Free buffer
				free(buf); 
			}
		}else{
			#ifdef DEBUG
			printf ("[DEBUG] Nothing to be sent with slice %d\n", i); 
			#endif 
		}
	}

	// All good
	return 0; 
}	

// Serve a given amount of data 
int serveData(SSL *ssl, int s,  int data_size, char *proto){ 
	
	// Logging
	#ifdef DEBUG
	printf ("[DEBUG] Requested data with size %d\n", data_size); 
	#endif 
	// Send data via SPP/SSL
	int still_to_send = data_size; 
	for (;;){
		// Derive min between how much to send and max buffer size 
		int toSend = BUFTLS; 
		if (still_to_send < toSend){
			toSend = still_to_send; 
		}

		//Allocate buffer with size to send and fill with "!"
		char *buf = (char*) malloc(sizeof(char) * toSend);
		memset(buf, '!', sizeof(char) * toSend);	
		#ifdef VERBOSE
		printf ("[DEBUG] Buffer=\n%s\n", buf); 
		#endif 
		
		// Send <<buf>> on SPP/SSL connection 
		if (strcmp(proto, "spp") == 0){
			sendData(ssl, s, buf, proto, sizeof(char) * toSend); 
		}
		if (strcmp(proto, "ssl") == 0){
			int r = SSL_write(ssl, buf, toSend);
			check_SSL_write_error(ssl, r, toSend);
		}
		if (strcmp(proto, "pln") == 0){
			sendData(ssl, s, buf, proto, sizeof(char) * toSend); 
		}
		
		// Update how much content is still to send 
		still_to_send -= toSend;
		if (still_to_send == 0){
			#ifdef DEBUG
			printf ("[DEBUG] No more data to send\n"); 
			#endif 
			break; 	
		}

		// Free buffer
		free(buf); 
	}

	// All good
	return 0; 
}	

// Serve a file -- solution extracted from original s_client 
// NOTES: (1) modified not to use BIO ; (2) re-negotiation currently not used
int serveFile(SSL *ssl, int s, char *filename, char *proto, char *data){ 
	
 	FILE *fp;
 	FILE *fw;				// file descriptot 
	int file_size = 0;		// size of file being sent 
 	

	
	// Open requested file for reading
 	
 	if ((fp = fopen(filename,"r")) == NULL){
		
		fp = fopen( filename , "wb" );

				
		#ifdef DEBUG
		printf("[PROXY-SSL] CACHING BUFFER FOR <<%s>>...\n", filename);
		#endif
		#ifdef DEBUG
		printf("[PROXY-SSL] THE BUFFER LOOK LIKE:\n %s \n", data);
		#endif
		/*
		#ifdef DEBUG
		printf("[PROXY-SSL] THE BUFFER SIZE %d BYTES\n", strlen(data));
		#endif
		*/
		fwrite(data, strlen(data), sizeof(data), fp);
		
		//fclose(fp);
		/*
		#ifdef DEBUG
		printf("[PROXY-SSL] FILE <<%s>> CREATED\n", filename);
		#endif
		*/
		
		int request_len = strlen(data);
		// Calculate file size 
		// Seek  to the end of the file and ask for position 
		//fseek(fp, 0L, SEEK_END);  //establece la posicion file del fp (lectura de filename) al final del fichero
		file_size = ftell(fp);   //retorna la posicion actual del fichero, en este caso es el final del fichero
		                          //(SEEK_END)
		// Seek to the beginning of the file 
		//fseek(fp, 0L, SEEK_SET);  //vuelve a poner la posicion del file al comienzo del fichero (SEEK_SET)
		
		//#ifdef DEBUG
		//printf ("[DEBUG] File requested is <<%s>> with size <<%d bytes>>\n", filename, file_size); 
		//#endif 
		fclose(fp);
	
		if((fp = fopen(filename,"r")) != NULL){
				// Calculate file size 
		
			// Seek  to the end of the file and ask for position 
			fseek(fp, 0L, SEEK_END);  //establece la posicion file del fp (lectura de filename) al final del fichero
			file_size = ftell(fp);    //retorna la posicion actual del fichero, en este caso es el final del fichero
		                          //(SEEK_END)
			// Seek to the beginning of the file 
			fseek(fp, 0L, SEEK_SET);  //vuelve a poner la posicion del file al comienzo del fichero (SEEK_SET)
		
			#ifdef DEBUG
			printf ("[DEBUG] File requested is <<%s>> with size <<%d bytes>>\n", filename, file_size); 
			#endif 

			// Allocate buffer for data to send 
			char* buf = (char*) malloc (file_size);  //guarda en memoria 
	
			// Read "toSend" content from file 
			int i = fread(buf, file_size, 1, fp);    //lee los datos de fichero fp, buf=puntero de memoria,file_size=tamaño
	                                         //del numero de elementos leidos
	                                         //1=número de elementos, los cuales tienen un tamaño "size"
	                                         //fp es el Puntero par aun
	                                         //objeto FILE, que especifica la cadena de entrada.
			// Check for end of file
			if (i <= 0){
				#ifdef DEBUG
				printf("[DEBUG] Done reading file %s\n", filename); 	
				#endif
			}else{
				#ifdef DEBUG
				printf("[DEBUG] Still some data to read? (read result = %d)\n", i); 	
				#endif
			}
			// Send <<buf>> on SPP/SSL connection 
			sendData(ssl, s, buf, proto, file_size); 
			
			// Close file
			fclose(fp);
			int ret=0;

			if (ret == 0){
				ret = remove(filename);	
			}
			else{
				ret=0;
				#ifdef DEBUG
				printf("[PROXY-SPP] ERROR IN CACHING"); 	
				#endif
			}

		}
	}
	else{
		// Calculate file size 
		// Seek  to the end of the file and ask for position 
		fseek(fp, 0L, SEEK_END);  //establece la posicion file del fp (lectura de filename) al final del fichero
		file_size = ftell(fp);    //retorna la posicion actual del fichero, en este caso es el final del fichero
		                          //(SEEK_END)
		// Seek to the beginning of the file 
		fseek(fp, 0L, SEEK_SET);  //vuelve a poner la posicion del file al comienzo del fichero (SEEK_SET)
		
		#ifdef DEBUG
		printf ("[DEBUG] File requested is <<%s>> with size <<%d bytes>>\n", filename, file_size); 
		#endif 
		// Allocate buffer for data to send 
		char* buf = (char*) malloc (file_size);  //guarda en memoria 
	
		// Read "toSend" content from file 
		int i = fread(buf, file_size, 1, fp);    //lee los datos de fichero fp, buf=puntero de memoria,file_size=tamaño
	                                         //del numero de elementos leidos
	                                         //1=número de elementos, los cuales tienen un tamaño "size"
	                                         //fp es el Puntero par aun
	                                         //objeto FILE, que especifica la cadena de entrada.

		// Check for end of file
		if (i <= 0){
			#ifdef DEBUG
			printf("[DEBUG] Done reading file %s\n", filename); 	
			#endif
		}else{
			#ifdef DEBUG
			printf("[DEBUG] Still some data to read? (read result = %d)\n", i); 	
			#endif
			}

		// Send <<buf>> on SPP/SSL connection 
		sendData(ssl, s, buf, proto, file_size); 
		// Close file
		fclose(fp);
	}
	
	/*
	// Allocate buffer for data to send 
	char* buf = (char*) malloc (file_size);  //guarda en memoria 
	
	// Read "toSend" content from file 
	int i = fread(buf, file_size, 1, fp);    //lee los datos de fichero fp, buf=puntero de memoria,file_size=tamaño
	                                         //del numero de elementos leidos
	                                         //1=número de elementos, los cuales tienen un tamaño "size"
	                                         //fp es el Puntero par aun
	                                         //objeto FILE, que especifica la cadena de entrada.
	// Check for end of file
	if (i <= 0){
		#ifdef DEBUG
		printf("[DEBUG] Done reading file %s\n", filename); 	
		#endif
	}else{
		#ifdef DEBUG
		printf("[DEBUG] Still some data to read? (read result = %d)\n", i); 	
		#endif
	}
	// Send <<buf>> on SPP/SSL connection 
	sendData(ssl, s, buf, proto, file_size); 
		
	// Close file
	fclose(fp);
	*/

	// All good
	return 0; 
}	


// serve requests in browser-like mode 
static int http_serve_request_browser(SSL *ssl, int s, char *proto){
  
    int r; 
	char buf[BUFSIZZ];
	
	// Clean buffer 
	memset(buf, 0, sizeof(buf));

	// Read HTTP GET (assuming a single read is enough)
	while(1){
		if (strcmp(proto, "pln") == 0){
			r = read(s, buf, BUFSIZZ);
			if (r < 0){
				berr_exit("[ERROR] TCP read problem - exit");
			}
		} else{
			if (strcmp(proto, "spp") == 0){
				SPP_SLICE *slice;       
				SPP_CTX *ctx;           
				r = SPP_read_record(ssl, buf, BUFSIZZ, &slice, &ctx);
			} else if (strcmp(proto, "ssl") == 0){
				r = SSL_read(ssl, buf, BUFSIZZ);
			}
			long error = SSL_get_error(ssl, r); 
			if ( error == SSL_ERROR_ZERO_RETURN){
				#ifdef DEBUG
				printf("[DEBUG] Client closed the connection\n");
				#endif
				return -1; 
			} else if (error != SSL_ERROR_NONE){
				char tempBuf[100]; 
				ERR_error_string(error, tempBuf); 
				//berr_exit("[ERROR] SSL read problem - exit");
				#ifdef DEBUG
				printf("[DEBUG] Client closed the connection or error %s\n", tempBuf);
				#endif
				return -1; 
			}
		}
		#ifdef DEBUG
		printf("[DEBUG] GET request is %d bytes long:\n%s\n", r, buf); 
		#endif
		
		//Look for the blank line that signals the end of the HTTP header (FIXME: assume 1 read is enough)
		if(strstr(buf, "\r\n") || strstr(buf, "\n")){
			break; 
		}else{
			return 0;  
		}
	}

	// extract request sizes 
    char **s_Token;
    int  size_alloc; 
	int j; 
	int c = TokenizeString(buf, &s_Token,  &size_alloc, '\n');
	for (j = 0; j < c ; j++){
		#ifdef DEBUG
		printf ("%d -- %s\n", j, s_Token[j]);
		#endif
	}
    char *left = s_Token[0]; 
	
	#ifdef DEBUG    
    printf("[DEBUG] Extracted first line from request: %s\n", left); 
    #endif 

    char **s_Token1;
	int c1 = TokenizeString(left, &s_Token1, &size_alloc, ' ');
	for (j = 0; j < c1 ; j++){
		printf ("%d -- %s\n", j, s_Token1[j]);
	}
    char *fn = s_Token1[1]; 
	
	#ifdef DEBUG    
    printf("[DEBUG] List of requested slice sizes: %s\n", fn); 
    #endif 
	
    char **s_Token2;
    long response_len = 0; 
    int count = TokenizeString(fn, &s_Token2, &size_alloc, '_');
	int i;
    #ifdef DEBUG
	printf("[DEBUG] Found %d tokens\n", count);
	for (i=0; i <count; i++) {
		printf("%s ", s_Token2[i]);
	}
	printf("\n");
    #endif
    long resp_len_arr[count]; 
	for(i=0; i < count; i++){
		resp_len_arr[i] = atol(s_Token2[i]); 
    	response_len += resp_len_arr[i]; 
		#ifdef DEBUG
		printf("[DEBUG] Requested Slice %d with size %ld\n", i, resp_len_arr[i]);
		#endif 
    }

	#ifdef DEBUG
	printf("[DEBUG] Total size is %ld\n", response_len);
	#endif 
	
	// serve requested data 
	if (strcmp(proto, "spp") == 0){
		serveDataSPPBrowser(ssl, s,  response_len, proto, resp_len_arr, count); 
	} else {	
		// use classic method to serve data if not SPP
		#ifdef DEBUG
		printf("[DEBUG] Classic method to serve %ld data\n", response_len);
		#endif 
		serveData(ssl, s, response_len, proto); 
	}

	// free memory 	
	for (j = 0; j < c ; j++){
		free (s_Token[j]); 
	}
	for (j = 0; j < c1 ; j++){
		free (s_Token1[j]); 
	}
	for (j = 0; j < count ; j++){
		free (s_Token2[j]); 
	}
	free(s_Token); 
	free(s_Token1); 
	free(s_Token2); 
	// All good 
    return 0; 
}




// Serve a file -- solution extracted from original s_client 
// NOTES: (1) modified not to use BIO ; (2) re-negotiation currently not used
int serveFile_old(SSL *ssl, int s, char *filename, char *proto){ 
	
 	FILE *fp;				// file descriptot 
	char buf[BUFSIZZ];		// buffer for data to send
	int file_size = 0;		// size of file being sent 
	int still_to_send; 		// amount of data from file still to be sent 
 
	// Open requested file for reading
 	if ((fp = fopen(filename,"r")) == NULL){
		#ifdef DEBUG
		printf ("File <<%s>> do not exist\n", filename); 
		#endif 
		char *data = "Error opening file\r\n"; 
		int request_len = strlen(data);
		sendData(ssl, s, data, proto, request_len); 
		fclose(fp); 
		return -1; 
	}else{
		// Calculate file size 
		// Seek  to the end of the file and ask for position 
		fseek(fp, 0L, SEEK_END);
		file_size = ftell(fp);
		// Seek to the beginning of the file 
		fseek(fp, 0L, SEEK_SET);
		
		#ifdef DEBUG
		printf ("[DEBUG] File requested is <<%s>> with size <<%d bytes>>\n", filename, file_size); 
		#endif 
	}

	// Transfer file via SPP/SSL
	still_to_send = file_size; 
	for (;;){
		
		// Derive min between how much to send and max buffer size 
		int toSend = BUFSIZZ; 
		if (still_to_send < toSend){
			toSend = still_to_send; 
		}

		// Read "toSend" content from file 
		int i = fread(buf, toSend, 1, fp);

		// Check for end of file
		if (i <= 0){
			#ifdef DEBUG
			printf("[DEBUG] Done reading file %s\n", filename); 	
			#endif
			break; 
		}else{
			#ifdef DEBUG
			printf("[DEBUG] Read %d bytes from file\n", toSend); 	
			#endif
		}

		// Update how much content is still to send 
		still_to_send -= (i * toSend);
		
		// Send <<buf>> on SPP/SSL connection 
		if (strcmp(proto, "spp") == 0){
			int request_len = strlen(buf);
			sendData(ssl, s, buf, proto, request_len); 
		}
		if (strcmp(proto, "ssl") == 0){
			int r = SSL_write(ssl, buf, toSend);
			check_SSL_write_error(ssl, r, toSend);
		}
	}

	// Close file
	fclose(fp); 

		/* CODE FOR RENOGOTIATION 
		// Check if too many losses 
		if (total_bytes > (3 * file_size)){
			total_bytes = 0;
			fprintf(stderr,"RENEGOTIATE\n");
			SSL_renegotiate(ssl); 
		}
		
		// Renegotiation if too many lossed
		for (j = 0; j < i; ){
			// After 13 attempts, re-negotate at SSL level 
			static int count = 0; 
			if (++count == 13) { 
				SSL_renegotiate(ssl); 
			} 

			int r = SSL_write(ssl, buf, BUFSIZZ);
			check_SSL_write_error(ssl, r, BUFSIZZ);
			//int k = BIO_write(io, &(buf[j]), i-j);
			if (k <= 0){
				if (! BIO_should_retry(io)){
					goto write_error; 
				}else {
					BIO_printf(io, "rewrite W BLOCK\n");
					}
				}else{
					j += k;
				}
			write_error:
				BIO_free(file);
				break; 
			}

		if((r = BIO_flush(io))<0){
			err_exit("Error flushing BIO");
		}
    } // end for loop 
	*/
	
	// All good
	return 0; 
}	



// 1) work with both SSL and SPP
// 2) no usage of BIO APIs
static int http_serve_request(SSL *ssl, int s, char *proto, bool shut, int action, SSL* struct_ssl){
  
    int r, write_to_ssl; 
	char buf[BUFSIZZ];    //buffer de datos
	char buffreturn[BUFSIZZ];
	char *filename = ""; 
	int ret_value = 0; 

	// Read HTTP GET (assuming a single read is enough)
	while(1){
		if (strcmp(proto, "spp") == 0){   //si son iguales entra aqui
			SPP_SLICE *slice;       //slice para SPP_read
			SPP_CTX *ctx;           //contexto para SPP_read   
			r = SPP_read_record(ssl, buf, BUFSIZZ, &slice, &ctx);
			if (SSL_get_error(ssl, r) != SSL_ERROR_NONE)
				berr_exit("[DEBUG] SSL read problem");
			#ifdef DEBUG
			printf("[DEBUG] Read %d bytes\n", r);
			#endif
			

			//ESCRIBO DATOS EN SSL
			#ifdef DEBUG
			printf("[PROXY-SSL] WRITING DATA IN SSL\n");
			#endif
			write_to_ssl = SSL_write(struct_ssl, buf, r);
			check_SSL_write_error(struct_ssl, write_to_ssl, r);
			#ifdef DEBUG
			printf("[PROXY-SSL] FORWARDING INFORMATION TO SERVER SSL\n");
			#endif


		}
		else if (strcmp(proto, "ssl") == 0){
			r = SSL_read(ssl, buf, BUFSIZZ);   //lee los bytes de una conexion TLS/SSL, ssl es el estructura
			                                   //intenta leer numero de bytes (BUFSIZZ) a partir de la
			                                    //estrucutura ssl especificada en el buffer (buf)
			if (SSL_get_error(ssl, r) != SSL_ERROR_NONE)
				berr_exit("[DEBUG] SSL read problem");
		}
		else if (strcmp(proto, "pln") == 0){
			r = read(s, buf, BUFSIZZ);
		}

		#ifdef DEBUG
		printf("[DEBUG] Request received on SPP:\n"); 
		printf("%s\n", buf); 
		#endif


		//Look for the blank line that signals the end of the HTTP header
		if(strstr(buf, "\r\n") || strstr(buf, "\n")){
			break; 
		}
	}

	// Parse filename from HTTP GET
	filename = parse_http_get(buf); 
	int fSize = atoi(filename);  //convirte un argumento string en un entero (int), cuando el valor
	                             //sean letras o palabras
	                             // entonces retorna cero, quiere decir que la conversion y el resultado es 0
	if (fSize != 0 || filename[0] == '0'){
		#ifdef DEBUG
		printf("[DEBUG] File requested by size <<%d>>\n", fSize);
		#endif
	} else {
		#ifdef DEBUG
		printf("[DEBUG] File requested by name <<%s>>\n", filename); 
		#endif
	}

	
	//LEYENDO DATOS EN SSL
			int i;
			for (i=0; i<=1; i++) {
			r = SSL_read(struct_ssl, buffreturn, sizeof(buffreturn));
			#ifdef DEBUG
			printf("RETURN RECEIVED ON SSL:\n %s \n", buffreturn);
			#endif
			}
	

	// Simple trick to end 
	if (strcmp(filename, "-1") == 0){
		#ifdef DEBUG
		printf("[DEBUG] Client requested to end session\n");
		#endif 
		ret_value = -1; 
		shut = true;  	
	}else{
		// Serve requested file either by name or by size  
		if (action == 3){
			if (fSize == 0){
				/*
				#ifdef DEBUG
				printf("ENTRA!!\n");
				#endif
				*/
				serveFile(ssl, s, filename, proto, buffreturn); 
			} else {
				serveData(ssl, s,  fSize, proto); 
			}
		}
		if (action == 4){
			// convert filename into data size 
			int data_size; 
			sscanf(filename, "%d", &data_size); 

			// serve data with size above 
			serveData(ssl, s,  data_size, proto); 
		}
	}

	// Do not shutdown TLS for browser-like behavior unless requested -- FIXME (NEVER SHUTDOWN)
	if (shut){
		#ifdef DEBUG
		printf("[DEBUG] Shutdown SSL connection\n");
		#endif 
		// Shutdown SSL - TO DO (check what happens here) 
		r = SSL_shutdown(ssl);
		if( !r ){
			shutdown(s, 1);
			r = SSL_shutdown(ssl);
	    }

		// Verify that all went good 
		switch(r){  
			case 1:
    	   		break; // Success
			case 0:
			case -1:
			default: // Error 
				#ifdef DEBUG 
				printf ("Shutdown failed with code %d\n", r); 
				#endif 
				berr_exit("Shutdown failed");
		}

		// free SSL 
    	SSL_free(ssl);
	
		// Close socket
    	close(s);
	}else{
		#ifdef DEBUG
		printf("[DEBUG] No shutdown since SSL connection might still be used\n");
		#endif
	}
	// All good 
    return ret_value; 
}


// SSL http serve (almost original function) 
static int http_serve_SSL(SSL *ssl, int s){
  
	char buf[BUFSIZZ];
	int r; //len; //len seems useless...
	BIO *io,*ssl_bio;
    
	io = BIO_new(BIO_f_buffer());	
	ssl_bio = BIO_new(BIO_f_ssl());
	BIO_set_ssl(ssl_bio, ssl, BIO_CLOSE);
	BIO_push(io, ssl_bio);
    	
	while(1){
		r = BIO_gets(io, buf, BUFSIZZ-1);
	
		
		if (SSL_get_error(ssl, r) == SSL_ERROR_NONE){
		} else {
			berr_exit("SSL read problem");
		}
		
		//Look for the blank line that signals the end of the HTTP headers //
		if(!strcmp(buf, "\r\n") || !strcmp(buf, "\n")){
			break; 
		}
	}
    
	// Put 200 OK on the wire 
	if((r = BIO_puts (io, "HTTP/1.0 200 OK\r\n")) <= 0){
		err_exit("Write error");
	}

	// Put server name on the wire 
    if((r = BIO_puts (io,"Server: Svarvel\r\n\r\n")) <= 0){
		err_exit("Write error");
	}
	
	if((r=BIO_puts (io,"Server test page\r\n"))<=0){
		err_exit("Write error");
	}
  	
	// Send file index.html -- TO DO, extend to a requested name
	BIO *file;
	static int bufsize = BUFSIZZ;
	int total_bytes = 0, j = 0, file_size = 0; 

	// Determine file size -- TO DO: integration with BIO stuff 
	file_size = calculate_file_size("index.html"); 
	
	// Open requested file 
	if ((file = BIO_new_file("index.html","r")) == NULL){                
		BIO_puts(io, "Error opening file"); // what is text? ERROR
        BIO_printf(io,"Error opening index.html\r\n");
		goto write_error;
	}

	// Put file on the wire 
	for (;;){
		// Read bufsize from requested file 
		int i = BIO_read(file, buf, bufsize);
		if (i <= 0){
			break; 
		}

		// Keep count of bytes sent on the wire 
		total_bytes += i;
		
		// Check if too many losses 
		if (total_bytes > (3 * file_size)){
			total_bytes = 0;
			fprintf(stderr,"RENEGOTIATE\n");
			SSL_renegotiate(ssl); 
		}
		
		// ??
		for (j = 0; j < i; ){
			// After 13 attempts, re-negotate at SSL level 
			static int count = 0; 
			if (++count == 13) { 
				SSL_renegotiate(ssl); 
			} 

			int k = BIO_write(io, &(buf[j]), i-j);
			if (k <= 0){
				if (! BIO_should_retry(io)){
					goto write_error; 
				}else {
					BIO_printf(io, "rewrite W BLOCK\n");
					}
				}else{
					j += k;
				}
			write_error:
				BIO_free(file);
				break; 
			}

		if((r = BIO_flush(io))<0){
			err_exit("Error flushing BIO");
		}
		
		r = SSL_shutdown(ssl);
		if( !r ){
      /* If we called SSL_shutdown() first then
         we always get return value of '0'. In
         this case, try again, but first send a
         TCP FIN to trigger the other side's
         close_notify*/
			shutdown(s,1);
			r = SSL_shutdown(ssl);
    	}
		switch(r){  
			case 1:
        		break; /* Success */
			case 0:
			case -1:
			default:
				#ifdef DEBUG 
				printf ("Shutdown failed with code %d\n", r); 
				#endif 
				berr_exit("Shutdown failed");
		}
    } // end for loop 

    SSL_free(ssl);
    close(s);

    return(0);
  }

// Usage function 
void usage(void){
	printf("usage: wserver -c -o -s\n");
	printf("-c:   protocol requested: ssl, spp, pln, fwd, spp-mod, ssl-mod, pln-mod, fwd-mod.\n");
	printf("-o:   {1=test handshake ; 2=200 OK ; 3=file transfer ; 4=browser-like behavior}\n");
	printf("-s:   content slicing strategy {uni; cs}\n");
	printf("-l:   duration of load estimation time (10 sec default)\n");
	printf("{uni[DEFAUL]=split response equally among slices ; cs=split uniformly among half slices, assuming other half is used by the client}\n");
	exit(-1);
}


// Main function  
int main(int argc, char **argv){
	int sock, newsock;                 // socket descriptors 
	BIO *sbio;
	SSL_CTX *ctx;
	SSL *ssl;
	int r;
	pid_t pid;
	char *proto;                        // protocol type 
	extern char *optarg;                // user input parameters
	int c;                              // user iput from getopt
	int action = 0;                     // specify client/server behavior (handshake, 200OK, serve file, browser-like) 
	int status;                         // ...
	clock_t start, end;                 // timers for cpu time estimation 
	struct timespec tps, tpe;
	double cpu_time_used;               // cpu time used 
	int loadTime = 10;                  // time used for load estimation (10 second default, user can change with option -l)

	////////////////////////////
	SSL_CTX *ssl_ctx;
	int sock_new;
	int socket_proxy;
	BIO *sbiossl;
	BIO *sbiospp;
	SSL *struct_ssl;

	// Handle user input parameters
	while((c = getopt(argc, argv, "c:o:s:l:")) != -1){
		switch(c){
			// Protocol 
			case 'c':	if(! (proto = strdup(optarg) )){
							err_exit("Out of memory");
						}
						if (strcmp(proto, "spp_mod") == 0){
							proto = "spp"; 
							disable_nagle = 1;
						}
						if (strcmp(proto, "ssl_mod") == 0){
							proto = "ssl"; 
							disable_nagle = 1;
						}
						if (strcmp(proto, "fwd_mod") == 0){
							proto = "fwd"; 
							disable_nagle = 1;
						}
						if (strcmp(proto, "pln_mod") == 0){
							proto = "pln"; 
							disable_nagle = 1;
						}
						if (strcmp(proto, "fwd") == 0){
							proto = "ssl"; 
						}
						break; 

			// Client/Server behavior 
			case 'o':	action = atoi(optarg); 
						if (action == 2)
							action = 3;
						break; 

			// Control slicing strategy
			case 's':	if(! (strategy = strdup(optarg) )){
							err_exit("Out of memory");
						}
						break;
			// Control load estimation period 
			case 'l':	loadTime = atoi(optarg); 
						break;
		}
	}

	// Check that input parameters are correct 
	if (argc == 1){
		usage();
	}
	if (action < 1 || action > 4){
		usage(); 
	}
	
	// Logging input parameters 
	#ifdef DEBUG
	printf("[DEBUG] Parameters count: %d\n", argc);
	char *temp_str = "undefined"; 
	if (action == 1)
		temp_str = "handshake_only";  
	if (action == 2)
		temp_str = "200_OK";  
	if (action == 3)
		temp_str = "serve_file";  
	if (action == 4)
        temp_str = "browser_like";  
	printf("\t[DEBUG] proto=%s; action=%d (%s)\n", proto, action, temp_str); 
	#endif 

	// Build SSL context
	ctx = initialize_ctx(KEYFILE, PASSWORD, proto);   //crea e inicializa contexto
	load_dh_params(ctx,DHFILE);

	#ifdef DEBUG
	printf("[PROXY-SSL] CREATING CONTEXT SSL FOR CONVERSION\n");
	#endif
	ssl_ctx = initialize_ctx(KEYFILE, PASSWORD, "ssl");
	load_dh_params(ssl_ctx,DHFILE);
   
	// Socket in listen state
	sock = tcp_listen(); //crea socket con ip de la maquina y lo pone a escuchar conexiones entrantes


	// Wait for client request 
	long finishLoadEst = (long) time (NULL) + loadTime;
	int nConn = 0; 
	bool report = true; 
	while(1){
			
		#ifdef DEBUG
		printf("[DEBUG] Waiting on TCP accept...\n"); 
		#endif
		if((newsock = accept(sock, 0, 0)) < 0){   //acepta conexion, en caso de que existan problemas 
			                                       //con el resultado que retorne
			                                      //tcp_listen(), es decir, sock, entonces saca error.
			err_exit("Problem socket accept\n");
		}else{
			#ifdef DEBUG
			printf("[DEBUG] Accepted new connection %d\n", sock); 
			#endif
		}
		// keep track of number of connections
		nConn++;

		// Fork a new process
		signal(SIGCHLD, SIG_IGN); 
		pid = fork();   //retorna valor negativo si el child proccess no fue existoso
		                //retorna valor cero cuando el child process se haya creado con exito
		if (pid == 0){
			/* In chil process */
			if (pid == -1) {
				berr_exit("FORK error"); 
				return 1;
           	}
			

			#ifdef HI_DEF_TIMER
           	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tps);
           	#else
           	start = clock();
           	#endif

			#ifdef DEBUG
			printf("[DEBUG] child process close old socket and operate on new one\n");
			#endif
			close(sock);   //cierra el socket 

			if (strcmp(proto, "pln") != 0) 
			{
				//AÑADIENDO CONVERSIÓN SPP A SSL
				sock_new = tcp_connect("192.168.122.7",443);

				//sock_new = tcp_connect("192.168.122.7",4443);
		        sbiossl = BIO_new_socket(sock_new, BIO_NOCLOSE);
				struct_ssl = SSL_new(ssl_ctx);
				SSL_set_bio(struct_ssl, sbiossl, sbiossl);

				#ifdef DEBUG
		        printf("[PROXY-SSL] CONNECTING TO SERVER SSL...\n");
		        #endif
				if(SSL_connect(struct_ssl) <= 0)
					berr_exit("SSL connect error");
    			#ifdef DEBUG
    			printf("[PROXY-SSL] SSL CONNECTED\n");
    			#endif

				sbio = BIO_new_socket(newsock, BIO_NOCLOSE);  //crea un bio socket a partir de newsocket
															  //newsocket es la variable que guarda la
				                                              //aceptacion de conexiones del sock
				ssl = SSL_new(ctx);  ////crea una nueva estructura ssl para una conexion
				SSL_set_bio(ssl, sbio, sbio);//conecta la BIO's sbio(lectura) y sbio(escritura) para
				                             //poder leer y escribir operaciones de TLS/SSL
				 
				// Wait on SSL Accept 
				if((r = SSL_accept(ssl) <= 0)){   //espera para inicar el handshake TLS/SSL
					berr_exit("SSL accept error");
				} else {
					#ifdef DEBUG
					if (strcmp(proto, "ssl") == 0){ 		
						printf("[DEBUG] SSL accept OK\n"); 
					}else{
						printf("[DEBUG] SPP accept OK\n"); 
					}
					#endif
				}
			}
			#ifdef HI_DEF_TIMER
			clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &tpe);
			cpu_time_used =  ( tpe.tv_sec - tps.tv_sec ) + (double)( tpe.tv_nsec - tps.tv_nsec )/ (double)1000000000L;
			#else
			end = clock();
			cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
			#endif


			if (loadTime > 0){
				printf( "CPU time=%g sec\n", cpu_time_used); 
			}
  			
			// Switch across possible client-server behavior 
			// NOTE: here we can add more complex strategies
			switch(action){
				// Handshake only 
				case 1: break; 
				
				// Respond with 200 OK
				case 2: http_simple_response(ssl, newsock, proto);
						break; 
				
				// Serve some content 
				case 3: http_serve_request(ssl, newsock, proto, false, action, struct_ssl);
						break;
			
				// Serve a browser like behavior 
				// NOTE
				// This can only serve one connection at a time. 
				// Here we would need to fire a new thread 
				// (or re-think the code) to support multiple SSL connections 
				// FIXME - detect closed connection 
				case 4: while(1){
							if (http_serve_request_browser(ssl, newsock, proto) < 0){
								break; 
							}
						}
						break;

				// Unknown option 
				default: usage();
						 break; 
			}
			// Correctly end child process
			#ifdef DEBUG
			printf("[DEBUG] End child process (prevent zombies)\n");
			#endif
			exit(0);  
			// return 0 
		}else{
			#ifdef DEBUG
			printf("[DEBUG] parent process close new socket\n");
			#endif
			close(newsock); 
		}
	}
	wait(&status);
	// Clean context
	destroy_ctx(ctx);
	
	// Correctly end parent process
	exit(0); 
}
