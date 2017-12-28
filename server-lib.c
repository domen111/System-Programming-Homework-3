// ======================================================================================================
// You don't need to know how the following codes are working

#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/mman.h>

static void add_to_buf( http_request *reqP, char* str, size_t len );
static void strdecode( char* to, char* from );
static int hexit( char c );
static char* get_request_line( http_request *reqP );
static void* e_malloc( size_t size );
static void* e_realloc( void* optr, size_t size );

static void init_request( http_request* reqP ) {
    reqP->conn_fd = -1;
    reqP->status = 0;       // not used
    reqP->file[0] = (char) 0;
    reqP->query[0] = (char) 0;
    reqP->host[0] = (char) 0;
    reqP->buf = NULL;
    reqP->buf_size = 0;
    reqP->buf_len = 0;
    reqP->buf_idx = 0;
}

static void free_request( http_request* reqP ) {
    if ( reqP->buf != NULL ) {
    free( reqP->buf );
    reqP->buf = NULL;
    }
    init_request( reqP );
}


#define ERR_RET( error ) { *errP = error; return -1; }
// return 0: success, file is buffered in retP->buf with retP->buf_len bytes
// return -1: error, check error code (*errP)
// return 1: read more, continue until return -1 or 0
// error code:
// 1: client connection error
// 2: bad request, cannot parse request
// 3: method not implemented
// 4: illegal filename
// 5: illegal query
// 6: file not found
// 7: file is protected
//
static int read_header_and_file( http_request* reqP, fd_set *master_set, int *errP ) {
    // Request variables
    char* file = (char *) 0;
    char* path = (char *) 0;
    char* query = (char *) 0;
    char* protocol = (char *) 0;
    char* method_str = (char *) 0;
    int r, fd;
    struct stat sb;
    char buf[10000];
    void *ptr;

    // Read in request from client
    while (1) {
        r = read( reqP->conn_fd, buf, sizeof(buf) );
        if ( r < 0 && ( errno == EINTR || errno == EAGAIN ) ) return 1;
        if ( r <= 0 ) ERR_RET( 1 )
        add_to_buf( reqP, buf, r );
        if ( strstr( reqP->buf, "\015\012\015\012" ) != (char*) 0 ||
            strstr( reqP->buf, "\012\012" ) != (char*) 0 ) break;
    }

    // Parse the first line of the request.
    method_str = get_request_line( reqP );
    if ( method_str == (char*) 0 ) ERR_RET( 2 )
    path = strpbrk( method_str, " \t\012\015" );
    if ( path == (char*) 0 ) ERR_RET( 2 )
    *path++ = '\0';
    path += strspn( path, " \t\012\015" );
    protocol = strpbrk( path, " \t\012\015" );
    if ( protocol == (char*) 0 ) ERR_RET( 2 )
    *protocol++ = '\0';
    protocol += strspn( protocol, " \t\012\015" );
    query = strchr( path, '?' );
    if ( query == (char*) 0 )
        query = "";
    else
        *query++ = '\0';

    if ( strcasecmp( method_str, "GET" ) != 0 ) ERR_RET( 3 )
    else {
        strdecode( path, path );
        if ( path[0] != '/' ) ERR_RET( 4 )
        else file = &(path[1]);
    }

    if ( strlen( file ) >= MAXBUFSIZE-1 ) ERR_RET( 4 )
    if ( strlen( query ) >= MAXBUFSIZE-1 ) ERR_RET( 5 )

    strcpy( reqP->file, file );
    strcpy( reqP->query, query );

    fprintf( stderr, "query: %s file: %s\n", query, file );

    r = stat( reqP->file, &sb );
    if ( r < 0 ) ERR_RET( 6 )

    fd = open( reqP->file, O_RDONLY );
    if ( fd < 0 ) ERR_RET( 7 )

    if ( query[0] == (char) 0 ) {
        ptr = mmap( 0, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
        if ( ptr == (void*) -1 ) ERR_RET( 8 )
        write_http_response( reqP, (char*)ptr, sb.st_size );
        (void) munmap( ptr, sb.st_size );
        close( fd );
        return 0;
    } else {
        int in_fd[2], out_fd[2];
        pipe(in_fd);
        pipe(out_fd);
        if ( fork() == 0 ) {
            dup2(in_fd[0], 0);
            dup2(out_fd[1], 1);
            // fprintf(stderr, "exec\n");
            close(in_fd[0]); close(in_fd[1]); close(out_fd[0]); close(out_fd[1]);
            execl(file, file, (char*)0);
        } else {
            close(in_fd[0]); close(out_fd[1]);
            write(in_fd[1], query, strlen(query));
            close(in_fd[1]);
            FD_SET(out_fd[0], master_set);
            pipe_fd_to_reqP[out_fd[0]] = reqP;
            fprintf(stderr, "pipe_fd_to_reqP[%d] = %p\n", out_fd[0], reqP );
        }
        return 2;
    }

    return 0;
}

void write_http_response( http_request *reqP, char *str, size_t len ) {
    fprintf(stderr, "write_http_response\n");
    int buflen;
    char timebuf[100];
    time_t now;
    char buf[10000];

    reqP->buf_len = 0;

    buflen = snprintf( buf, sizeof(buf), "HTTP/1.1 200 OK\015\012Server: SP TOY\015\012" );
    add_to_buf( reqP, buf, buflen );
    now = time( (time_t*) 0 );
    (void) strftime( timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", gmtime( &now ) );
    buflen = snprintf( buf, sizeof(buf), "Date: %s\015\012", timebuf );
    add_to_buf( reqP, buf, buflen );
    buflen = snprintf( buf, sizeof(buf), "Content-Length: %ld\015\012", (int64_t) len );
    add_to_buf( reqP, buf, buflen );
    buflen = snprintf( buf, sizeof(buf), "Connection: close\015\012\015\012" );
    add_to_buf( reqP, buf, buflen );

    add_to_buf( reqP, str, len );
    printf( "%s\n", reqP->buf );
    fflush( stdout );
    reqP->buf_idx = 0; // writing from offset 0
}


static void add_to_buf( http_request *reqP, char* str, size_t len ) {
    char** bufP = &(reqP->buf);
    size_t* bufsizeP = &(reqP->buf_size);
    size_t* buflenP = &(reqP->buf_len);

    if ( *bufsizeP == 0 ) {
    *bufsizeP = len + 500;
    *buflenP = 0;
    *bufP = (char*) e_malloc( *bufsizeP );
    } else if ( *buflenP + len >= *bufsizeP ) {
    *bufsizeP = *buflenP + len + 500;
    *bufP = (char*) e_realloc( (void*) *bufP, *bufsizeP );
    }
    (void) memmove( &((*bufP)[*buflenP]), str, len );
    *buflenP += len;
    (*bufP)[*buflenP] = '\0';
}

static char* get_request_line( http_request *reqP ) {
    int begin;
    char c;

    char *bufP = reqP->buf;
    int buf_len = reqP->buf_len;

    for ( begin = reqP->buf_idx ; (int)reqP->buf_idx < buf_len; ++reqP->buf_idx ) {
        c = bufP[ reqP->buf_idx ];
        if ( c == '\012' || c == '\015' ) {
            bufP[reqP->buf_idx] = '\0';
            ++reqP->buf_idx;
            if ( c == '\015' && (int)reqP->buf_idx < buf_len &&
                bufP[reqP->buf_idx] == '\012' ) {
            bufP[reqP->buf_idx] = '\0';
            ++reqP->buf_idx;
            }
            return &(bufP[begin]);
        }
    }
    fprintf( stderr, "http request format error\n" );
    exit( 1 );
}



static void init_http_server( http_server *svrP, unsigned short port ) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname( svrP->hostname, sizeof( svrP->hostname) );
    svrP->port = port;

    svrP->listen_fd = socket( AF_INET, SOCK_STREAM, 0 );
    if ( svrP->listen_fd < 0 ) ERR_EXIT( "socket" )

    bzero( &servaddr, sizeof(servaddr) );
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl( INADDR_ANY );
    servaddr.sin_port = htons( port );
    tmp = 1;
    if ( setsockopt( svrP->listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*) &tmp, sizeof(tmp) ) < 0 )
    ERR_EXIT ( "setsockopt " )
    if ( bind( svrP->listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr) ) < 0 ) ERR_EXIT( "bind" )

    if ( listen( svrP->listen_fd, 1024 ) < 0 ) ERR_EXIT( "listen" )
}

// Set NDELAY mode on a socket.
static void set_ndelay( int fd ) {
    int flags, newflags;

    flags = fcntl( fd, F_GETFL, 0 );
    if ( flags != -1 ) {
    newflags = flags | (int) O_NDELAY; // nonblocking mode
    if ( newflags != flags )
        (void) fcntl( fd, F_SETFL, newflags );
    }
}

static void strdecode( char* to, char* from ) {
    for ( ; *from != '\0'; ++to, ++from ) {
    if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) ) {
        *to = hexit( from[1] ) * 16 + hexit( from[2] );
        from += 2;
    } else {
        *to = *from;
        }
    }
    *to = '\0';
}


static int hexit( char c ) {
    if ( c >= '0' && c <= '9' )
    return c - '0';
    if ( c >= 'a' && c <= 'f' )
    return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
    return c - 'A' + 10;
    return 0;           // shouldn't happen
}


static void* e_malloc( size_t size ) {
    void* ptr;

    ptr = malloc( size );
    if ( ptr == (void*) 0 ) {
    (void) fprintf( stderr, "out of memory\n" );
    exit( 1 );
    }
    return ptr;
}


static void* e_realloc( void* optr, size_t size ) {
    void* ptr;

    ptr = realloc( optr, size );
    if ( ptr == (void*) 0 ) {
        (void) fprintf( stderr, "out of memory\n" );
        exit( 1 );
    }
    return ptr;
}
