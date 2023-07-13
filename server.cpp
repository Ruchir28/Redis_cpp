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
#include <vector>
#include <fcntl.h>
#include <poll.h>
#include <unordered_map>

const size_t k_max_msg = 4096;

enum
{
    STATE_REQ = 0,
    STATE_RES = 1,
    STATE_END = 2
};


struct Conn
{
    int fd = -1;
    u_int32_t state = 0;
    // buffer for reading
    size_t rbuf_size = 0;
    u_int8_t rbuf[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    u_int8_t wbuf[4 + k_max_msg];
};

static void state_req(Conn* conn);
static void state_res(Conn* conn);

static void msg(const char *msg)
{
    fprintf(stderr, "%s\n", msg);
}

static void die(const char *msg)
{
    int err = errno;
    fprintf(stderr, "[%d] %s\n", err, msg);
    abort();
}

/**
 * set file desciptor to non blocking
 */
static void fd_set_nb(int fd)
{
    errno = 0;
    // get the control flags for this descriptor
    int flags = fcntl(fd, F_GETFL, 0);
    if (errno)
    {
        die("fcntl error");
        return;
    }
    // set the non blocking bit as true
    flags |= O_NONBLOCK;

    errno = 0;
    // set the control flags for the descriptor
    (void)fcntl(fd, F_SETFL, flags);
    if (errno)
    {
        die("fcntl error");
    }
}

static bool try_one_request(Conn *conn) {
    // trying to parse request from buffer
    if(conn -> rbuf_size < 4) {
        // even the lenght of the message isn't available,
        // which is the first 4 bytes so returning false here
        return false;
    }
    uint32_t len = 0;
    memcpy(&len,conn->rbuf,4);
    if(len > k_max_msg) {
        msg("message too long");
        conn->state = STATE_END;
        return false;
    }

    if(4 + len > conn->rbuf_size) {
        return false;
    }

    // got the message, print it out 
    printf("client says: %.*s",len,conn->rbuf + 4);

    // generating echo message 
    memcpy(conn->wbuf,conn->rbuf,4);
    memcpy(conn->wbuf + 4,conn->rbuf + 4,len);
    conn->wbuf_size = 4 + len;

    // remove the request from the buffer.
    size_t remain = conn->rbuf_size - 4 - len;
    if (remain) {
        memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
    }
    conn->rbuf_size = remain;

    // change state
    conn->state = STATE_RES;
    // sending the response
    state_res(conn);

    return conn->state == STATE_REQ;
}

static bool try_fill_buffer(Conn *conn) {
    assert(conn->rbuf_size < sizeof(conn->rbuf));
    ssize_t rv = 0;
    do {
        size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
        rv = read(conn->fd,conn->rbuf + conn->rbuf_size,cap);
    } while(rv < 0 && errno == EINTR);
    if(rv < 0 && errno == EAGAIN) {
        // buffer was not filled, i.e no data was available.
        return false;
    }
    if (rv < 0) {
        msg("read() error");
        conn->state = STATE_END;
        return false;
    }
    if(rv == 0) {
        // rv will be 0 when we amount of data read was 0.
        if(conn->rbuf_size > 0) {
            msg("Unexpected EOF");
        } else {
            msg("EOF");
        }
        conn->state = STATE_END;
        return false;
    }  

    conn->rbuf_size += (size_t)rv;
    assert(conn->rbuf_size <= sizeof(conn->rbuf));

    // TODO: RESUME FROM HERE 

    // Try to process requests one by one.
    while (try_one_request(conn)) {}
    return (conn->state == STATE_REQ);
}

static bool try_flush_buffer(Conn *conn) {
    ssize_t rv = 0;
    do {
        size_t remain = conn->wbuf_size - conn->wbuf_sent;
        rv = write(conn->fd,conn->wbuf + conn->wbuf_sent,remain);
    } while(rv < 0 && errno == EINTR);
    if(rv < 0 && errno == EAGAIN) {
        return false;
    }
    if(rv < 0) {
        msg("write() error");
        conn->state = STATE_END;
        return false;
    }
    conn->wbuf_sent += (size_t)rv;
    assert(conn->wbuf_sent  <= conn->wbuf_size);
    if(conn->wbuf_sent == conn->wbuf_size) {
        conn->wbuf_size = 0;
        conn->wbuf_sent = 0;
        conn->state = STATE_REQ;
        return false;
    }
    return true;
}

static void state_req(Conn *conn) {
    while (try_fill_buffer(conn)) {}
}
static void state_res(Conn *conn) {
    while (try_flush_buffer(conn)) {
    }
}

static void connection_io(Conn *conn) {
    if (conn->state == STATE_REQ) {
        state_req(conn);
    } else if (conn->state == STATE_RES) {
        state_res(conn);
    } else {
        assert(0);  // not expected
    }
}

static int32_t accept_new_conn(std::unordered_map<int, Conn *> &fd2Conn,int fd) {
    printf("Accepting new connection \n");
    // accept 
    struct sockaddr_in client_addr;
    socklen_t socklen  = sizeof(client_addr);
    int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
    if (connfd < 0) {
        msg("accept() error");
        return -1;  // error
    }

    // setting the new connections socket to non-blocking
    fd_set_nb(connfd);

    // creating the struct connection
    struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
    if(!conn) {
        close(connfd);
        return -1;
    }
    conn->fd = connfd;
    conn->state = STATE_REQ;
    conn->rbuf_size = 0;
    conn->wbuf_size = 0;
    conn->wbuf_sent = 0;
    fd2Conn[connfd] = conn;
    return 0;
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

    rv = listen(serverSocket, SOMAXCONN);

    if (rv)
    {
        die("listen()");
    }

    printf("Server Started \n");

    // maps the functional descriptor to the connection
    std::unordered_map<int, Conn *> fd2Conn;

    fd_set_nb(serverSocket);
    
    // list of pollfd i.e fd with their io status, used by poll to identify,
    // which fd is ready for I/O.
    std::vector<struct pollfd> poll_args;

    while (true)
    {
        // clearing the event loop and then readding all the pollfd for
        // connection as the state for a connection might have changed
        // and depending on that it will be decided whether's it's read, write or closed.
        poll_args.clear();

        // adding the current socket descriptor to listen to incoming connections
        struct pollfd pfd = {
            serverSocket,
            POLLIN,
            0};

        poll_args.push_back(pfd);
        // connection fds
        for (auto it : fd2Conn)
        {
            Conn *conn = it.second;
            if (!conn)
            {
                continue;
            }
        
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

        // Calling the poll function to wait for an event on any of the file descriptors in the poll_args vector.
        // The third argument is the timeout, which is set to 1000 milliseconds.
        // If no event occurs within the timeout, poll will return 0.
        // polling for active fd's and if there are no active fd's with the timeout specified,
        // i.e 1000ms then it will return an error
        int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);

        if (rv < 0)
        {
            die("poll()");
        }

        for (size_t i = 1; i < poll_args.size(); i++)
        {
            // if the revents field of a pollfd structure is non zero, it means
            // the corresponding fd is ready for an io
            if (poll_args[i].revents)
            {
                printf("Connection ready for io");
                Conn *conn = fd2Conn[poll_args[i].fd];
                connection_io(conn); //TODO: performing the io
                if (conn->state == STATE_END){
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2Conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }
        }
        // try to accept a new connection if the listening fd is active
        if (poll_args[0].revents)
        {
            accept_new_conn(fd2Conn, serverSocket);
        }
    }

    return 0;
}
