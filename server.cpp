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
    u_int8_t rbuf_size[4 + k_max_msg];
    // buffer for writing
    size_t wbuf_size = 0;
    size_t wbuf_sent = 0;
    u_int8_t wbuf[4 + k_max_msg];
};

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
 * Read N bytes from the file descriptor (i.e socket in this case)
 * and write to buffer
 */
static int32_t read_full(int socketDescriptor, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = read(socketDescriptor, buf, n); // will return the number of bytes read
        if (rv <= 0)
        {
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
static int32_t write_all(int sockerDescriptor, char *buf, size_t n)
{
    while (n > 0)
    {
        ssize_t rv = write(sockerDescriptor, buf, n);
        if (rv <= 0)
        {
            return -1;
        }
        assert((size_t)rv <= n);
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

static void do_something(int connfd)
{
    char rbuf[4 + k_max_msg + 1];
    ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
    if (n < 0)
    {
        msg("read() error");
        return;
    }
    printf("client says: %s\n", rbuf);

    char wbuf[] = "world";
    write(connfd, wbuf, strlen(wbuf));
}

static int32_t one_request(int socketFd)
{
    char rbuf[4 + k_max_msg + 1];
    errno = 0;
    int32_t err = read_full(socketFd, rbuf, 4);
    if (err)
    {
        // encountered EOF without reading intended number of bytes
        if (errno == 0)
        {
            msg("EOF");
        }
        else
        {
            msg("read() error");
        }
        return err;
    }

    uint32_t len = 0;
    memcpy(&len, rbuf, 4);
    if (len > k_max_msg)
    {
        msg("too long");
        return -1;
    }

    // request body
    err = read_full(socketFd, rbuf + 4, len);

    if (err)
    {
        msg("read() error");
        return err;
    }

    rbuf[4 + len] = '\0';
    printf("Message Received :: %s\n", &rbuf[4]);

    // replying

    const char reply[] = "[ACK]";

    char wbuf[4 + sizeof(reply)];

    len = (uint32_t)strlen(reply);

    memcpy(wbuf, &len, 4);
    memcpy(wbuf + 4, reply, sizeof(reply));

    return write_all(socketFd, wbuf, 4 + len);
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

            // Calling the poll function to wait for an event on any of the file descriptors in the poll_args vector.
            // The third argument is the timeout, which is set to 1000 milliseconds.
            // If no event occurs within the timeout, poll will return 0.
            struct pollfd pfd = {};
            pfd.fd = conn->fd;
            pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
            pfd.events = pfd.events | POLLERR;
            poll_args.push_back(pfd);
        }

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
                Conn *conn = fd2Conn[poll_args[i].fd];
                // connection_io(conn); //TODO: performing the io
                if (conn->state == STATE_END){
                    // client closed normally, or something bad happened.
                    // destroy this connection
                    fd2Conn[conn->fd] = NULL;
                    (void)close(conn->fd);
                    free(conn);
                }
            }

            // try to accept a new connection if the listening fd is active
            if (poll_args[0].revents)
            {
                // accept_new_conn(fd2conn, fd);
            }
        }
    }

    return 0;
}
