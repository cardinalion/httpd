/*
A simple http server.
*/

#include <stdio.h>
#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stddef.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define ISspace(x) isspace((int)(x))
#define SERVER_STRING "Server: ywhttpd/0.1.0\r\n"
#define CONTENT_TYPE "Content-Type: text/html\r\n\r\n"

#define BUFSIZE 1024
#define METHODSIZE 255
#define URLSIZE 255
#define PATHSIZE 512
#define ENVSIZE 255

int initialize(uint16_t *);
void accept_request(int);
int get_line(int, char *, int);
void serve_file(int, const char *);
void headers(int, const char *);
void send_files(int, FILE *);
void execute_cgi(int, const char *, const char *, const char *);
void error_die(const char *);
void discard_headers(int);

void bad_request(int);    // 400
void not_found(int);      // 404
void cannot_execute(int); // 500
void not_supported(int);  // 501

int main()
{

    int server_socket = -1;
    uint16_t port = 0;
    int client_socket = -1;

    /* 
    struct sockaddr {
        unsigned short   sa_family;        2
        char             sa_data[14];      14
    };
    struct sockaddr_in {
        short int            sin_family;   2
        unsigned short int   sin_port;     2
        struct in_addr       sin_addr;     4
        unsigned char        sin_zero[8];  8
    }; // IP-based communication
    struct in_addr {
        unsigned long s_addr;
    };
    */
    struct sockaddr_in client_name;
    int client_name_len = sizeof(client_name);
    server_socket = initialize(&port);
    printf("httpd running on port %d\n", port);

    while (1)
    {

        /*
        int accept(int socket, struct sockaddr *restrict address,
            socklen_t *restrict address_len);
        */
        client_socket = accept(server_socket,
                               (struct sockaddr *)&client_name,
                               &client_name_len);
        if (client_socket == -1)
            error_die("error in accepting client socket");
        accept_request(client_socket);
    }
    close(server_socket);
    return 0;
}

int initialize(uint16_t *port)
{

    int httpd = 0;
    struct sockaddr_in name;
    int namelen = sizeof(name);
    /* int socket(int domain, int type, int protocol); */
    /* AF_* == PF_* address family vs protocol family */
    /* specify protocol number if there are multiple otherwise 0 */
    httpd = socket(PF_INET, SOCK_STREAM, 0);
    if (httpd == -1)
        error_die("error in initializing socket");

    memset(&name, 0, namelen);                // all 0
    name.sin_family = AF_INET;                // internet domain socket
    name.sin_port = htons(*port);             // convert from host byte order to network byte order
    name.sin_addr.s_addr = htonl(INADDR_ANY); // bind to all available interfaces

    /*
    int bind(int socket, const struct sockaddr *address,
        socklen_t address_len);
    */
    if (bind(httpd, (struct sockaddr *)&name, namelen))
        error_die("error in binding");
    if (*port == 0)
    {

        /*
        int getsockname(int socket, struct sockaddr *address,
            socklen_t *address_len);
        // returns the current addres bounded
        */
        if (getsockname(httpd, (struct sockaddr *)&name, &namelen))
            error_die("error in getsockname");
        *port = ntohs(name.sin_port); // 如何动态分配端口
    }

    /* int listen(int socket, int backlog); */
    if (listen(httpd, 5))
        error_die("error in listening");
    return httpd;
}

void accept_request(int client)
{

    char buf[BUFSIZE];
    char method[METHODSIZE];
    char url[URLSIZE];
    char path[PATHSIZE];
    char *query_string = NULL;
    int cgi = 0;
    size_t i = 0, j = 0; // i for method or url, j for buf
    int numchars = get_line(client, buf, BUFSIZE);

    /*
    struct stat {
        dev_t     st_dev;         // ID of device containing file
        ino_t     st_ino;         // Inode number
        mode_t    st_mode;        // File type and mode
        nlink_t   st_nlink;       // Number of hard links
        uid_t     st_uid;         // User ID of owner
        gid_t     st_gid;         // Group ID of owner
        dev_t     st_rdev;        // Device ID (if special file)
        off_t     st_size;        // Total size, in bytes
        blksize_t st_blksize;     // Block size for filesystem I/O
        blkcnt_t  st_blocks;      // Number of 512B blocks allocated
        struct timespec st_atim;  // Time of last access
        struct timespec st_mtim;  // Time of last modification
        struct timespec st_ctim;  // Time of last status change

    #define st_atime st_atim.tv_sec      // Backward compatibility
    #define st_mtime st_mtim.tv_sec
    #define st_ctime st_ctim.tv_sec
    };
    */
    struct stat st;

    while (!ISspace(buf[j]) && (i < METHODSIZE - 1))
    {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        not_supported(client); // don't support, return 501
        return;
    }

    i = 0;
    while (ISspace(buf[j]) && (j < BUFSIZE))
        j++;
    while (!ISspace(buf[j]) && (i < URLSIZE - 1) && (j < BUFSIZE))
    {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if (!strcasecmp(method, "POST"))
    {
        cgi = 1;
    }
    else
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
            query_string++;
        if (*query_string == '?')
        {
            cgi = 1;
            *query_string = '\0'; // end of url
            query_string++;
        }
    }
    sprintf(path, "htdocs%s", url);
    if (path[strlen(path) - 1] == '/')
        strcat(path, "index.html");

    /* 
    int stat(const char *pathname, struct stat *statbuf); 
    get file status
    */
    if (stat(path, &st))
    {
        discard_headers(client);
        not_found(client);
    }
    else
    {
        if ((st.st_mode & S_IFMT) == S_IFDIR)
            strcat(path, "/index.html");
        if ((st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH))
            cgi = 1;
        if (!cgi)
        {
            discard_headers(client);
            serve_file(client, path);
        }
        else
        {
            if (strcasecmp(method, "POST"))
                discard_headers(client);
            execute_cgi(client, path, method, query_string);
        }
    }

    close(client);
}

int get_line(int client, char *buf, int size)
{

    int n; // recv return val
    int i = 0;
    char c = '\0'; // buffer of length 1

    while ((i < size - 1) && (c != '\n'))
    {

        /* ssize_t recv(int socket, void *buffer, size_t length, int flags); */
        n = recv(client, &c, 1, 0);
        if (n > 0)
        {

            // possibly "\r\n"
            if (c == '\r')
            {
                n = recv(client, &c, 1, MSG_PEEK); // data treated as unread
                if ((n > 0) && (c == '\n'))
                {
                    recv(client, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return i;
}

void serve_file(int client, const char *path)
{

    FILE *resource = NULL;
    int numchars = 1;
    char buf[BUFSIZE];

    while ((numchars > 0) && strcmp("\n", buf))
    {
        numchars = get_line(client, buf, sizeof(buf));
    }

    resource = fopen(path, "r");
    if (resource == NULL)
    {
        not_found(client);
    }
    else
    {
        headers(client, path);
        send_files(client, resource);
    }
    fclose(resource);
}

void headers(int client, const char *path)
{

    char buf[BUFSIZE];

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    strcat(buf, SERVER_STRING);
    /* ssize_t send(int socket, const void *buffer, size_t length, int flags); */
    send(client, buf, strlen(buf), 0);
    strcpy(buf, CONTENT_TYPE);
    send(client, buf, strlen(buf), 0);
}

void send_files(int client, FILE *resource)
{

    char buf[BUFSIZE];

    while (fgets(buf, BUFSIZE, resource))
    {
        send(client, buf, strlen(buf), 0);
    }
}

void execute_cgi(int client, const char *path, const char *method, const char *query_string)
{

    char buf[BUFSIZE];
    int numchars;
    int content_length = -1;
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;

    if (strcasecmp(method, "GET"))
    {

        numchars = get_line(client, buf, BUFSIZE);
        while ((numchars > 0) && strcmp("\n", buf))
        {

            buf[15] = '\0';
            if (strcasecmp(buf, "Content-Length:") == 0)
                content_length = atoi(&(buf[16]));
            numchars = get_line(client, buf, sizeof(buf));
        }
        if (content_length == -1)
        {
            bad_request(client);
            return;
        }
    }

    strcpy(buf, "HTTP/1.0 200 OK\r\n");
    send(client, buf, strlen(buf), 0);

    if (pipe(cgi_output) || pipe(cgi_input) || ((pid = fork()) < 0))
    {
        cannot_execute(client);
        return;
    }
    if (pid == 0)
    { // child

        char meth_env[ENVSIZE];
        char query_env[ENVSIZE];
        char length_env[ENVSIZE];

        /* 
        int dup2(int oldfd, int newfd)
        creates a copy of the file descriptor 
        */
        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        /* 
        int close(int fd);
        closes a fd, so that it no longer refers to any file 
        */
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        /*
        int putenv(char *string);
        change or add an ENV
        */
        putenv(meth_env);
        if (!strcasecmp(method, "GET"))
        {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        }
        else
        {
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        /*
        int execl(const char *path, const char *arg, ...);
        must terminated by NULL pointer
        */
        execl(path, path, NULL);
        exit(0);
    }
    else
    { // parent

        close(cgi_output[1]);
        close(cgi_input[0]);
        if (!strcasecmp(method, "POST"))
        {
            for (i = 0; i < content_length; i++)
            {
                recv(client, &c, 1, 0);
                /*
                ssize_t write(int fd, const void *buf, size_t count);
                write to a fd
                */
                write(cgi_input[1], &c, 1);
            }
        }
        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        /*
        pid_t waitpid(pid_t pid, int *status, int options);
        wait for process to change state
        */
        waitpid(pid, &status, 0);
    }
}

void error_die(const char *err_msg)
{

    perror(err_msg);
    exit(1);
}

void discard_headers(int client)
{

    char buf[BUFSIZE];
    int numchars = 1;

    while ((numchars > 0) && strcmp("\n", buf))
    {
        numchars = get_line(client, buf, BUFSIZE);
    }
}

void bad_request(int client)
{

    char buf[BUFSIZE];

    strcpy(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, CONTENT_TYPE);
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, "<P>Your browser sent a bad request\r\n");
    send(client, buf, sizeof(buf), 0);
}

void not_found(int client)
{

    char buf[BUFSIZE];

    strcpy(buf, "HTTP/1.0 404 NOT FOUND\r\n");
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, CONTENT_TYPE);
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "<BODY>The server could not fulfill your request because the resource");
    strcat(buf, " specified is unavailable or nonexistent.</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}

void cannot_execute(int client)
{

    char buf[BUFSIZE];

    strcpy(buf, "HTTP/1.0 500 Internal Server Error\r\n");
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, CONTENT_TYPE);
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, "<P>Error prohibited CGI execution.\r\n");
    send(client, buf, sizeof(buf), 0);
}

void not_supported(int client)
{

    char buf[BUFSIZE];

    strcpy(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, CONTENT_TYPE);
    send(client, buf, sizeof(buf), 0);
    strcpy(buf, "<HTML><HEAD><TITLE>Method Not Implemented</TITLE></HEAD>\r\n");
    send(client, buf, strlen(buf), 0);
    strcpy(buf, "<BODY>HTTP request method not supported.</BODY></HTML>\r\n");
    send(client, buf, strlen(buf), 0);
}
