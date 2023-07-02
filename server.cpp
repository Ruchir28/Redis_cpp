#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/ip.h>


static void msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg) {
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}
/**
 * Read N bytes from the file descriptor (i.e socket in this case)
 * and write to buffer
*/
static int32_t read_full(int socketDescriptor,char *buf,size_t n) {
    while(n > 0) {
        ssize_t rv = read(socketDescriptor,buf,n); // will return the number of bytes read 
        if(rv <= 0) {
            // if it's less than 0 it signifies some erro 
            // so return abruptly
            return -1; 
        }
        assert((size_t)rv <= n); 
        n -= (size_t)rv;
        buf += rv; // advancing the buffer with number of bytes read
    }
    return 0;
}

/**
 * Read N bytes from the buffer and write to file descriptor 
*/
static int32_t write_all(int sockerDescriptor,char *buf,size_t n) {
    while(n > 0) {
        ssize_t rv = write(sockerDescriptor,buf,n);
        if(rv <= 0) {
            return -1; 
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    } 
    return 0;
}

static int32_t handleRequest(int connfd) {

}

static void do_something(int connfd) {
    char rbuf[64] = {};
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0) {
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}


int main()
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == -1)
    {
        printf("Falied to create socket \n !!");
    }
    int val = 1;
    /**
     * param1 -> client socket descriptor
     * param2 -> setting the level of socker to general, other options are tcp,ip 
     * param3 -> make the port reusable, i.e port can bind to multiple sockets
     */
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    // bind, this is the syntax that deals with IPv4 addresses
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ntohs(1234);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    int rv = bind(serverSocket, (const sockaddr *)&addr, sizeof(addr));
    if (rv)
    {
        die("bind()");
    }

    rv = listen(serverSocket,SOMAXCONN);

    if(rv) {
        die("listen()");
    }

   printf("Server Started");

    while(true) {
        sockaddr_in client_addr = {};
        socklen_t socklen = sizeof(client_addr);
        int connfd = accept(serverSocket, (struct sockaddr *)&client_addr, &socklen);
        if (connfd < 0) {
            continue;   // error
        }
        

        do_something(connfd);
        close(connfd);
    }
}