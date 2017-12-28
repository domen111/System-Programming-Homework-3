#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>

#define TIMEOUT_SEC 5       // timeout in seconds for wait for a connection
#define MAXBUFSIZE  1024    // timeout in seconds for wait for a connection
#define NO_USE      0       // status of a http request
#define ERROR       -1
#define READING     1
#define WRITING     2
#define ERR_EXIT(a) { perror(a); exit(1); }

typedef struct {
    char hostname[512];     // hostname
    unsigned short port;    // port to listen
    int listen_fd;          // fd to wait for a new connection
} http_server;

typedef struct {
    int conn_fd;            // fd to talk with client
    int status;             // not used, error, reading (from client)
                            // writing (to client)
    char file[MAXBUFSIZE];  // requested file
    char query[MAXBUFSIZE]; // requested query
    char host[MAXBUFSIZE];  // client host
    char* buf;              // data sent by/to client
    size_t buf_len;         // bytes used by buf
    size_t buf_size;        // bytes allocated for buf
    size_t buf_idx;         // offset for reading and writing
} http_request;

static char* logfilenameP;  // log file name

http_request **pipe_fd_to_reqP;
int *pipe_fd_to_pid;


// Forwards
//
static void init_http_server( http_server *svrP,  unsigned short port );
// initialize a http_request instance, exit for error

static void init_request( http_request* reqP );
// initialize a http_request instance

static void free_request( http_request* reqP );
// free resources used by a http_request instance

static int read_header_and_file( http_request* reqP, fd_set *master_set, int *errP );
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: continue to it until return -1 or 0
// error code:
// 1: client connection error
// 2: bad request, cannot parse request
// 3: method not implemented
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected

static void set_ndelay( int fd );
// Set NDELAY mode on a socket.

void write_http_response( http_request *reqP, char *str, size_t len, char *status, char *content_type );

int main( int argc, char** argv ) {
    http_server server;     // http server
    http_request* requestP = NULL;// pointer to http requests from client

    int maxfd;                  // size of open file descriptor table

    struct sockaddr_in cliaddr; // used by accept()
    int clilen;

    int conn_fd;        // fd for a new connection with client
    int err;            // used by read_header_and_file()
    int i, ret, nwritten;


    // Parse args.
    if ( argc != 3 ) {
        (void) fprintf( stderr, "usage:  %s port# logfile\n", argv[0] );
        exit( 1 );
    }

    logfilenameP = argv[2];

    // Initialize http server
    init_http_server( &server, (unsigned short) atoi( argv[1] ) );

    maxfd = getdtablesize();
    requestP = ( http_request* ) malloc( sizeof( http_request ) * maxfd );
    if ( requestP == (http_request*) 0 ) {
        fprintf( stderr, "out of memory allocating all http requests\n" );
        exit( 1 );
    }
    for ( i = 0; i < maxfd; i ++ )
        init_request( &requestP[i] );
    requestP[ server.listen_fd ].conn_fd = server.listen_fd;
    requestP[ server.listen_fd ].status = READING;
    pipe_fd_to_reqP = (http_request**) malloc( sizeof(http_request*) * maxfd);
    pipe_fd_to_pid = (int*) malloc( sizeof(int) * maxfd);

    fprintf( stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d, logfile %s...\n", server.hostname, server.port, server.listen_fd, maxfd, logfilenameP );

    // Main loop.
    fd_set master_set, working_set;
    FD_ZERO(&master_set);
    FD_SET(server.listen_fd, &master_set);
    while (1) {
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        memcpy(&working_set, &master_set, sizeof(fd_set));
        select(maxfd + 1, &working_set, NULL, NULL, &timeout);

        if (FD_ISSET(server.listen_fd, &working_set)) {
            fprintf(stderr, "wait conn\n"); fflush(stdout);
            // Wait for a connection.
            clilen = sizeof(cliaddr);
            conn_fd = accept( server.listen_fd, (struct sockaddr *) &cliaddr, (socklen_t *) &clilen );
            if ( conn_fd < 0 ) {
                if ( errno == EINTR || errno == EAGAIN ) continue; // try again
                if ( errno == ENFILE ) {
                    (void) fprintf( stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd );
                    continue;
                }
                ERR_EXIT( "accept" )
            }
            requestP[conn_fd].conn_fd = conn_fd;
            requestP[conn_fd].status = READING;
            strcpy( requestP[conn_fd].host, inet_ntoa( cliaddr.sin_addr ) );
            set_ndelay( conn_fd );

            fprintf( stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host );

            while (1) {
                ret = read_header_and_file( &requestP[conn_fd], &master_set, &err );
                if ( ret < 0 ) {
                    // error for reading http header or requested file
                    fprintf( stderr, "error on fd %d, code %d\n",
                        requestP[conn_fd].conn_fd, err );
                    if (err == 4) {
                        char message[] = "<h1>400 Bad Request</h1> <br> only [a-z A-Z _] are allow in cgi_program and filename";
                        write_http_response( &requestP[conn_fd], message, strlen(message), "400 Bad Request", "text/html; charset=utf-8" );
                    }
                    nwritten = write( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len );
                    fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                    requestP[conn_fd].status = ERROR;
                    close( requestP[conn_fd].conn_fd );
                    free_request( &requestP[conn_fd] );
                    break;
                } else if ( ret == 0 ) {
                    // ready for writing
                    fprintf( stderr, "writing (buf %p, idx %d) %d bytes to request fd %d\n",
                        requestP[conn_fd].buf, (int) requestP[conn_fd].buf_idx,
                        (int) requestP[conn_fd].buf_len, requestP[conn_fd].conn_fd );

                    // write once only and ignore error
                    nwritten = write( requestP[conn_fd].conn_fd, requestP[conn_fd].buf, requestP[conn_fd].buf_len );
                    fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, requestP[conn_fd].conn_fd );
                    close( requestP[conn_fd].conn_fd );
                    free_request( &requestP[conn_fd] );
                    break;
                } else if (ret == 2) break;
            }
        } else {
            for ( i = 4; i < maxfd; i++ ) {
                if (FD_ISSET( i, &working_set )) {
                    fprintf(stderr, "fd%d\n", i);
                    http_request *reqP = pipe_fd_to_reqP[i];
                    char *buf = (char*)malloc( sizeof(char)*1048576 );
                    int len = read( i, buf, 1048576 );
                    fprintf(stderr, "buf: %s\\buf\n", buf);
                    int stat;
                    waitpid(pipe_fd_to_pid[i], &stat, 0);
                    if (stat == 0) {
                        write_http_response( reqP, buf, len, "200 OK", "text/plain; charset=utf-8");
                    } else {
                        char message[] = "<h1>404 Not Found</h1> <br> cgi_program encounter an error.";
                        write_http_response( reqP, message, strlen(message), "404 Not Found", "text/html; charset=utf-8");
                    }
                    FD_CLR( i, &master_set );
                    close( i );
                    free(buf);

                    // ready for writing
                    fprintf( stderr, "writing (buf %p, idx %d) %d bytes to request fd %d\n",
                        reqP->buf, (int) reqP->buf_idx, (int) reqP->buf_len, reqP->conn_fd );

                    // write once only and ignore error
                    nwritten = write( reqP->conn_fd, reqP->buf, reqP->buf_len );
                    fprintf( stderr, "complete writing %d bytes on fd %d\n", nwritten, reqP->conn_fd );
                    close( reqP->conn_fd );
                    free_request( reqP );
                }
            }
        }
    }
    free( requestP );
    free( pipe_fd_to_reqP );
    free( pipe_fd_to_pid );
    return 0;
}

#include "server-lib.c"
