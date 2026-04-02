/*
 * 
 * mini_http_server.c — A Simple HTTP Server in C
 
 * Author  : Your Name (B.Tech CSE, 1st Year)
 * Purpose : Academic mini-project demonstrating TCP socket programming,
 *           HTTP request parsing, and static file serving in C.
 *
 * Compile : gcc -o server http_server.c
 * Run     : ./server
 * Test    : Open http://localhost:8080 in your browser
 
 */

/* Standard Library Headers  */
#include <stdio.h>          /* printf, fprintf, fopen, fread, fclose          */
#include <stdlib.h>         /* exit, malloc, free                             */
#include <string.h>         /* strlen, strcmp, strncpy, strstr, strcpy        */
#include <unistd.h>         /* close(), read(), write()                       */
#include <errno.h>          /* errno — error codes after failed system calls  */

/* Linux Socket / Network Headers  */
#include <sys/socket.h>     /* socket(), bind(), listen(), accept(), send()   */
#include <sys/types.h>      /* socklen_t and other POSIX types                */
#include <sys/stat.h>       /* stat() — check if a file exists                */
#include <netinet/in.h>     /* struct sockaddr_in, htons(), INADDR_ANY        */
#include <arpa/inet.h>      /* inet_ntoa() — convert IP to human-readable     */

/* ── Configuration Constants  */
#define PORT            8080    /* The port our server listens on              */
#define BACKLOG         10      /* Max pending connections in accept() queue   */
#define BUFFER_SIZE     4096    /* Size of the buffer for reading requests     */
#define MAX_PATH_LEN    256     /* Maximum length of a file path               */
#define MAX_FILE_SIZE   (1024 * 1024 * 10)  /* Max file we'll serve: 10 MB    */
/*
 * A MIME type tells the browser what kind of data is being sent.
 * e.g., "text/html" means "render this as a webpage".
 * We store file extension → MIME type pairs in this table.
 */
typedef struct {
    const char *extension;  /* File extension, e.g. ".html"  */
    const char *mime_type;  /* Corresponding MIME type string */
} MimeEntry;

static const MimeEntry MIME_TABLE[] = {
    { ".html", "text/html"        },
    { ".htm",  "text/html"        },
    { ".txt",  "text/plain"       },
    { ".css",  "text/css"         },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png"        },
    { ".jpg",  "image/jpeg"       },
    { ".jpeg", "image/jpeg"       },
    { ".gif",  "image/gif"        },
    { ".ico",  "image/x-icon"     },
    { NULL,    NULL               }   /* Sentinel — marks end of table        */
};


/* 
 * FUNCTION: get_mime_type
 * Walks the MIME_TABLE looking for an entry whose extension matches the
 * end of `filename`.  Returns a default MIME type if nothing matches.
 *
 * Parameters:
 *   filename — the name of the file being served (e.g. "index.html")
 *
 * Returns:
 *   A constant C-string containing the MIME type (e.g. "text/html")
 */
const char *get_mime_type(const char *filename) {
    /* Walk through every entry in the table until the sentinel (NULL, NULL) */
    for (int i = 0; MIME_TABLE[i].extension != NULL; i++) {
        /* strstr finds the extension string inside filename */
        if (strstr(filename, MIME_TABLE[i].extension) != NULL) {
            return MIME_TABLE[i].mime_type;
        }
    }
    /* Fallback: treat unknown files as raw binary data */
    return "application/octet-stream";
}


/* ═══════════════════════════════════════════════════════════════════════════
 * FUNCTION: parse_request_path
 * ───────────────────────────────────────────────────────────────────────────
 * Reads the raw HTTP request text and extracts:
 *   1. The HTTP method (GET, POST, …)
 *   2. The requested file path (/index.html, /style.css, …)
 *
 * A typical HTTP request first line looks like:
 *   GET /index.html HTTP/1.1
 *
 * Parameters:
 *   request — the full raw HTTP request string received from the browser
 *   method  — output buffer where we write the HTTP method
 *   path    — output buffer where we write the requested path
 *
 * Returns:
 *   1 on success (parsed okay), 0 on failure (malformed request)
 * ═══════════════════════════════════════════════════════════════════════════ */
int parse_request_path(const char *request, char *method, char *path) {
    /*
     * sscanf scans the request string and fills `method` and `path`
     * using the format string "  %s %s " which reads two space-separated
     * words.  %255s stops at 255 chars to prevent buffer overflow.
     */
    if (sscanf(request, "%255s %255s", method, path) != 2) {
        /* sscanf returns the number of successfully filled variables.
           If it's not 2, the request line is malformed. */
        return 0;
    }

    /*
     * Special case: if the browser requests just "/" (the root),
     * we redirect it to "/index.html" — the default webpage.
     */
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    return 1;  /* Success */
}


/* ═══════════════════════════════════════════════════════════════════════════
 * FUNCTION: send_404
 * ───────────────────────────────────────────────────────────────────────────
 * Sends a "404 Not Found" HTTP response to the client.
 * This is called whenever the browser requests a file that doesn't exist.
 *
 * The response includes a small, self-contained HTML error page.
 *
 * Parameters:
 *   client_fd — the socket file-descriptor connected to the browser
 *   path      — the path the browser asked for (used in the error message)
 * ═══════════════════════════════════════════════════════════════════════════ */
void send_404(int client_fd, const char *path) {
    /* Build the HTML body for the error page */
    char body[512];
    snprintf(body, sizeof(body),
        "<html><head><title>404 Not Found</title></head>"
        "<body style='font-family:monospace;padding:40px'>"
        "<h1>404 &mdash; Not Found</h1>"
        "<p>The file <code>%s</code> could not be found on this server.</p>"
        "<hr><small>mini_http_server / C</small>"
        "</body></html>",
        path);

    /* Build the full HTTP response (headers + body) */
    char response[1024];
    int  body_len = strlen(body);
    snprintf(response, sizeof(response),
        "HTTP/1.0 404 Not Found\r\n"          /* Status line              */
        "Content-Type: text/html\r\n"         /* Header: type of content  */
        "Content-Length: %d\r\n"              /* Header: size in bytes    */
        "Connection: close\r\n"               /* Header: no keep-alive    */
        "\r\n"                                /* Blank line (MANDATORY)   */
        "%s",                                 /* The HTML body            */
        body_len, body);

    /* send() pushes data into the socket and the OS delivers it */
    send(client_fd, response, strlen(response), 0);

    printf("  [Response] 404 Not Found — %s\n", path);
}


/* 
 * FUNCTION: send_400
 * Sends a minimal "400 Bad Request" response.
 * Called when the HTTP request is malformed or uses an unsupported method.
 */
void send_400(int client_fd) {
    const char *response =
        "HTTP/1.0 400 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>400 Bad Request</h1></body></html>";

    send(client_fd, response, strlen(response), 0);
    printf("  [Response] 400 Bad Request\n");
}


/*
 * FUNCTION: serve_file
 
 * The heart of our server.  Given a file path like "/index.html", this
 * function:
 *   1. Converts it to a local filesystem path (strips the leading "/")
 *   2. Opens the file in binary mode
 *   3. Reads its contents into memory
 *   4. Sends a "200 OK" HTTP response with the file contents
 *   5. Calls send_404() if the file doesn't exist
 *
 * Parameters:
 *   client_fd — connected socket to the browser
 *   path      — URL path from the HTTP request, e.g. "/index.html"
 */
void serve_file(int client_fd, const char *path) {
    /*
     * The URL path starts with '/'.  We skip that first character so
     * that "/index.html" becomes "index.html", which fopen() can use
     * to look for a file in the CURRENT directory.
     */
    const char *local_path = path + 1;   /* pointer arithmetic: skip '/' */

    printf("  [File]     Serving: %s\n", local_path);

    /* ── Check if the file exists ─────────────────────────────────────── */
    /*
     * struct stat holds file metadata.  stat() fills it; if stat() returns
     * -1, the file doesn't exist (or is inaccessible).
     */
    struct stat file_info;
    if (stat(local_path, &file_info) != 0) {
        send_404(client_fd, path);
        return;
    }

    /* ── Open the file ────────────────────────────────────────────────── */
    /*
     * "rb" = read binary.  Always use binary mode so that image files
     * and other non-text files are read correctly without any CR/LF
     * translation.
     */
    FILE *fp = fopen(local_path, "rb");
    if (fp == NULL) {
        /* File exists but we can't open it (permissions?) */
        fprintf(stderr, "  [Error]    fopen failed for %s: %s\n",
                local_path, strerror(errno));
        send_404(client_fd, path);
        return;
    }

    /* ── Determine file size ──────────────────────────────────────────── */
    long file_size = file_info.st_size;

    /* Safety guard: don't try to serve gigantic files */
    if (file_size > MAX_FILE_SIZE) {
        fprintf(stderr, "  [Error]    File too large: %ld bytes\n", file_size);
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    /* ── Read entire file into a heap-allocated buffer ────────────────── */
    /*
     * malloc() allocates `file_size` bytes on the heap.
     * We must free() this memory when we're done.
     */
    char *file_buf = (char *)malloc(file_size);
    if (file_buf == NULL) {
        fprintf(stderr, "  [Error]    malloc failed (out of memory?)\n");
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    /*
     * fread(buffer, item_size, item_count, file_pointer)
     * Reads `file_size` bytes (each of size 1) into file_buf.
     * Returns the number of bytes actually read.
     */
    size_t bytes_read = fread(file_buf, 1, file_size, fp);
    fclose(fp);   /* Always close the file after reading */

    if ((long)bytes_read != file_size) {
        fprintf(stderr, "  [Error]    Could only read %zu of %ld bytes\n",
                bytes_read, file_size);
        free(file_buf);
        send_404(client_fd, path);
        return;
    }

    /* ── Determine MIME type from file extension ──────────────────────── */
    const char *mime = get_mime_type(local_path);

    /* ── Build and send HTTP response headers ─────────────────────────── */
    char headers[512];
    int  header_len = snprintf(headers, sizeof(headers),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",           /* This blank line separates headers from body */
        mime, file_size);

    /*
     * Send headers first, then the file contents.
     * We send them as two separate send() calls.
     */
    send(client_fd, headers, header_len, 0);
    send(client_fd, file_buf, file_size, 0);

    printf("  [Response] 200 OK — %s (%ld bytes, %s)\n",
           local_path, file_size, mime);

    /* ── Clean up heap memory ─────────────────────────────────────────── */
    free(file_buf);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * FUNCTION: handle_client
 * ───────────────────────────────────────────────────────────────────────────
 * Called once per incoming browser connection.
 * Reads the HTTP request from the socket, parses it, and routes to the
 * appropriate handler (serve_file, send_404, or send_400).
 *
 * Parameters:
 *   client_fd   — socket connected to the browser
 *   client_addr — IP address info of the browser (for logging)
 * ═══════════════════════════════════════════════════════════════════════════ */
void handle_client(int client_fd, struct sockaddr_in client_addr) {
    char request_buf[BUFFER_SIZE];

    /* Print a log line showing which IP address connected */
    printf("\n[Connection] %s:%d\n",
           inet_ntoa(client_addr.sin_addr),  /* Converts binary IP → "127.0.0.1" */
           ntohs(client_addr.sin_port));     /* Network byte-order → host order  */

    /* ── Read the HTTP request from the socket ────────────────────────── */
    /*
     * recv() blocks until data arrives.  It fills request_buf and returns
     * how many bytes were received.  We leave the last byte for '\0'.
     */
    memset(request_buf, 0, sizeof(request_buf)); /* Zero the buffer first */
    ssize_t bytes_received = recv(client_fd, request_buf,
                                  sizeof(request_buf) - 1, 0);

    if (bytes_received <= 0) {
        fprintf(stderr, "  [Error]    recv() failed or client disconnected\n");
        close(client_fd);
        return;
    }

    /* Null-terminate so we can use string functions safely */
    request_buf[bytes_received] = '\0';

    /* Print just the first line of the request (the "request line") */
    char first_line[256] = {0};
    sscanf(request_buf, "%255[^\r\n]", first_line);   /* read until \r or \n */
    printf("  [Request]  %s\n", first_line);

    /* ── Parse method and path from the request ───────────────────────── */
    char method[16]  = {0};
    char path[MAX_PATH_LEN] = {0};

    if (!parse_request_path(request_buf, method, path)) {
        /* Couldn't parse the request — malformed HTTP */
        send_400(client_fd);
        close(client_fd);
        return;
    }

    /* ── Only handle GET requests ─────────────────────────────────────── */
    /*
     * strcmp returns 0 when the two strings are equal.
     * If the method is anything other than "GET", we respond with 400.
     */
    if (strcmp(method, "GET") != 0) {
        printf("  [Ignored]  Method not supported: %s\n", method);
        send_400(client_fd);
        close(client_fd);
        return;
    }

    /* ── Serve the requested file ─────────────────────────────────────── */
    serve_file(client_fd, path);

    /* ── Close the connection ─────────────────────────────────────────── */
    /*
     * close() releases the socket file descriptor.
     * The browser is notified the connection ended (TCP FIN handshake).
     */
    close(client_fd);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * FUNCTION: create_server_socket
 * ───────────────────────────────────────────────────────────────────────────
 * Creates and configures the TCP listening socket.
 * This is a one-time setup done before the server loop starts.
 *
 * Returns:
 *   The listening socket file descriptor (a non-negative integer)
 *   Exits the program on any error.
 * ═══════════════════════════════════════════════════════════════════════════ */
int create_server_socket(void) {

    /* ── Step 1: Create a socket ──────────────────────────────────────── */
    /*
     * socket(domain, type, protocol)
     *   AF_INET   = IPv4 internet protocol
     *   SOCK_STREAM = TCP (reliable, ordered, connection-based)
     *   0         = let the OS choose the default protocol (TCP for STREAM)
     *
     * Returns a "file descriptor" — an integer handle like fopen() returns
     * a FILE*.  Returns -1 on failure.
     */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket()");   /* perror prints the human-readable error */
        exit(EXIT_FAILURE);
    }
    printf("[Setup] Socket created (fd=%d)\n", server_fd);

    /* ── Step 2: Set SO_REUSEADDR ─────────────────────────────────────── */
    /*
     * After the server exits, the OS keeps the port "in use" for ~60 s
     * (TIME_WAIT state).  SO_REUSEADDR lets us immediately re-bind to
     * the same port after restarting — very convenient during development.
     */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt()");
        exit(EXIT_FAILURE);
    }

    /* ── Step 3: Bind the socket to an address + port ─────────────────── */
    /*
     * struct sockaddr_in describes an IPv4 address.
     *   sin_family      = AF_INET (IPv4)
     *   sin_addr.s_addr = INADDR_ANY → accept connections on ANY local IP
     *   sin_port        = htons(PORT) — htons converts host byte order to
     *                     network byte order (big-endian), required by TCP/IP
     */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));  /* Zero the struct */
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        perror("bind()");
        exit(EXIT_FAILURE);
    }
    printf("[Setup] Socket bound to port %d\n", PORT);

    /* ── Step 4: Put the socket into listening mode ───────────────────── */
    /*
     * listen() marks the socket as passive — ready to accept connections.
     * BACKLOG = max number of pending connections queued before accept().
     */
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen()");
        exit(EXIT_FAILURE);
    }
    printf("[Setup] Listening for connections (backlog=%d)\n", BACKLOG);

    return server_fd;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * FUNCTION: main
 * ───────────────────────────────────────────────────────────────────────────
 * Entry point.  Sets up the server socket and runs the accept() loop
 * that waits for browser connections forever (until you press Ctrl+C).
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {

    printf("╔══════════════════════════════════════╗\n");
    printf("║   mini_http_server  —  C / Linux     ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* Step A: Create the listening socket */
    int server_fd = create_server_socket();

    printf("\n[Ready] Server running at http://localhost:%d\n", PORT);
    printf("[Ready] Serving files from the current directory\n");
    printf("[Ready] Press Ctrl+C to stop\n\n");
    printf("──────────────────────────────────────────\n");

    /* ── Main server loop ─────────────────────────────────────────────── */
    /*
     * This loop runs FOREVER.  Each iteration:
     *   1. Blocks on accept() waiting for a browser to connect
     *   2. accept() returns a NEW socket specific to THAT client
     *   3. We handle the client, then loop back to accept the next one
     *
     * This is a SINGLE-THREADED server — it handles one client at a time.
     */
    while (1) {

        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        /*
         * accept() blocks here until a browser connects.
         * It returns a brand-new socket file descriptor for that client.
         * The original server_fd keeps listening for future connections.
         */
        int client_fd = accept(server_fd,
                               (struct sockaddr *)&client_addr,
                               &addr_len);

        if (client_fd < 0) {
            /* Accept can fail transiently; just log and continue */
            perror("accept()");
            continue;
        }

        /* Delegate to the handler function */
        handle_client(client_fd, client_addr);

        /*
         * Note: handle_client() calls close(client_fd) internally.
         * server_fd stays open — it's our permanent listening socket.
         */
    }

    /* Unreachable, but good practice */
    close(server_fd);
    return 0;
}
