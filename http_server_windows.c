/*
 * =============================================================================
 * http_server_windows.c — Mini HTTP Server (Windows Version)
 * =============================================================================
 *
 * Compile : gcc -o server http_server_windows.c -lws2_32 -Wall
 *           (the -lws2_32 flag links the Windows socket library)
 *
 * Run     : server.exe
 * Test    : http://localhost:8080
 *
 * CHANGES FROM LINUX VERSION:
 *   - Replaced sys/socket.h, netinet/in.h etc. with <winsock2.h>
 *   - Added WSAStartup() / WSACleanup() (Windows socket init/cleanup)
 *   - closesocket() instead of close()
 *   - recv()/send() work the same way
 *   - stat() still works (it's in <sys/stat.h> which MinGW has)
 *   - Everything else is identical
 * =============================================================================
 */

/* ── Windows Socket Header (replaces all Linux socket headers) ───────────── */
#include <winsock2.h>       /* socket, bind, listen, accept, send, recv, etc  */
#include <ws2tcpip.h>       /* inet_ntoa, sockaddr_in, etc.                   */

/* ── Standard C Headers (same as before) ────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>       /* stat() — MinGW on Windows supports this        */
#include <errno.h>

/* ── Tell linker to include ws2_32.lib automatically ────────────────────── */
#pragma comment(lib, "ws2_32.lib")  /* Works with MSVC; gcc uses -lws2_32 flag */

/* ── Configuration ───────────────────────────────────────────────────────── */
#define PORT          8080
#define BACKLOG       10
#define BUFFER_SIZE   4096
#define MAX_PATH_LEN  256
#define MAX_FILE_SIZE (1024 * 1024 * 10)

/* ── MIME Type Table ─────────────────────────────────────────────────────── */
typedef struct {
    const char *extension;
    const char *mime_type;
} MimeEntry;

static const MimeEntry MIME_TABLE[] = {
    { ".html", "text/html"               },
    { ".htm",  "text/html"               },
    { ".txt",  "text/plain"              },
    { ".css",  "text/css"                },
    { ".js",   "application/javascript"  },
    { ".json", "application/json"        },
    { ".png",  "image/png"               },
    { ".jpg",  "image/jpeg"              },
    { ".jpeg", "image/jpeg"              },
    { ".gif",  "image/gif"               },
    { ".ico",  "image/x-icon"            },
    { NULL,    NULL                      }
};

/* ═══════════════════════════════════════════════════════════════════════════
 * get_mime_type — same as Linux version, no changes needed
 * ═══════════════════════════════════════════════════════════════════════════ */
const char *get_mime_type(const char *filename) {
    for (int i = 0; MIME_TABLE[i].extension != NULL; i++) {
        if (strstr(filename, MIME_TABLE[i].extension) != NULL) {
            return MIME_TABLE[i].mime_type;
        }
    }
    return "application/octet-stream";
}

/* ═══════════════════════════════════════════════════════════════════════════
 * parse_request_path — same as Linux version, no changes needed
 * ═══════════════════════════════════════════════════════════════════════════ */
int parse_request_path(const char *request, char *method, char *path) {
    if (sscanf(request, "%255s %255s", method, path) != 2) {
        return 0;
    }
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    return 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * send_404 — same logic, but socket is SOCKET type (not int) on Windows
 * ═══════════════════════════════════════════════════════════════════════════ */
void send_404(SOCKET client_fd, const char *path) {
    /*
     * SOCKET is a typedef in winsock2.h
     * On Linux we used int; on Windows we use SOCKET
     * Everything else inside this function is identical
     */
    char body[512];
    snprintf(body, sizeof(body),
        "<html><head><title>404 Not Found</title></head>"
        "<body style='font-family:monospace;padding:40px'>"
        "<h1>404 &mdash; Not Found</h1>"
        "<p>The file <code>%s</code> could not be found.</p>"
        "<hr><small>mini_http_server / C (Windows)</small>"
        "</body></html>",
        path);

    char response[1024];
    int  body_len = strlen(body);
    snprintf(response, sizeof(response),
        "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, body);

    send(client_fd, response, strlen(response), 0);
    printf("  [Response] 404 Not Found - %s\n", path);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * send_400 — same, just SOCKET type instead of int
 * ═══════════════════════════════════════════════════════════════════════════ */
void send_400(SOCKET client_fd) {
    const char *response =
        "HTTP/1.0 400 Bad Request\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>400 Bad Request</h1></body></html>";

    send(client_fd, response, strlen(response), 0);
    printf("  [Response] 400 Bad Request\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * serve_file — same logic; only SOCKET type and closesocket() differ
 * ═══════════════════════════════════════════════════════════════════════════ */
void serve_file(SOCKET client_fd, const char *path) {
    const char *local_path = path + 1;   /* skip the leading '/' */
    printf("  [File]     Serving: %s\n", local_path);

    /* Check if file exists */
    struct stat file_info;
    if (stat(local_path, &file_info) != 0) {
        send_404(client_fd, path);
        return;
    }

    /* Open file in binary mode */
    FILE *fp = fopen(local_path, "rb");
    if (fp == NULL) {
        send_404(client_fd, path);
        return;
    }

    long file_size = file_info.st_size;

    if (file_size > MAX_FILE_SIZE) {
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    /* Read file into heap buffer */
    char *file_buf = (char *)malloc(file_size);
    if (file_buf == NULL) {
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    size_t bytes_read = fread(file_buf, 1, file_size, fp);
    fclose(fp);

    if ((long)bytes_read != file_size) {
        free(file_buf);
        send_404(client_fd, path);
        return;
    }

    /* Build and send headers */
    const char *mime = get_mime_type(local_path);
    char headers[512];
    int  header_len = snprintf(headers, sizeof(headers),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_size);

    send(client_fd, headers, header_len, 0);
    send(client_fd, file_buf, file_size, 0);

    printf("  [Response] 200 OK - %s (%ld bytes, %s)\n",
           local_path, file_size, mime);

    free(file_buf);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * handle_client — SOCKET type; closesocket() instead of close()
 * ═══════════════════════════════════════════════════════════════════════════ */
void handle_client(SOCKET client_fd, struct sockaddr_in client_addr) {
    char request_buf[BUFFER_SIZE];

    printf("\n[Connection] %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    memset(request_buf, 0, sizeof(request_buf));
    int bytes_received = recv(client_fd, request_buf,
                              sizeof(request_buf) - 1, 0);

    if (bytes_received <= 0) {
        closesocket(client_fd);   /* ← Windows uses closesocket(), not close() */
        return;
    }

    request_buf[bytes_received] = '\0';

    char first_line[256] = {0};
    sscanf(request_buf, "%255[^\r\n]", first_line);
    printf("  [Request]  %s\n", first_line);

    char method[16]         = {0};
    char path[MAX_PATH_LEN] = {0};

    if (!parse_request_path(request_buf, method, path)) {
        send_400(client_fd);
        closesocket(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {
        printf("  [Ignored]  Method not supported: %s\n", method);
        send_400(client_fd);
        closesocket(client_fd);
        return;
    }

    serve_file(client_fd, path);

    closesocket(client_fd);   /* ← closesocket() instead of close() */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * main — adds WSAStartup (Windows socket init) and WSACleanup at the end
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {

    printf("========================================\n");
    printf("   mini_http_server  (Windows / Winsock)\n");
    printf("========================================\n\n");

    /* ── WINDOWS ONLY: Initialize Winsock ────────────────────────────────
     * On Linux, sockets are ready immediately.
     * On Windows, you MUST call WSAStartup() first or nothing works.
     *
     * WSADATA  = a struct that Windows fills with socket library info
     * MAKEWORD(2,2) = request Winsock version 2.2 (the standard modern version)
     * ─────────────────────────────────────────────────────────────────── */
    WSADATA wsa_data;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0) {
        fprintf(stderr, "[Error] WSAStartup failed: %d\n", wsa_result);
        return 1;
    }
    printf("[Setup] Winsock initialized\n");

    /* ── Create TCP socket ───────────────────────────────────────────────
     * Same as Linux: AF_INET = IPv4, SOCK_STREAM = TCP
     * On Windows, socket() returns type SOCKET (not int)
     * INVALID_SOCKET is the Windows equivalent of Linux's -1
     * ─────────────────────────────────────────────────────────────────── */
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "[Error] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    printf("[Setup] Socket created\n");

    /* ── SO_REUSEADDR — same as Linux ───────────────────────────────────── */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));
    /*
     * Note: On Windows, setsockopt takes (const char*) not (void*)
     * The cast (const char *)&opt handles this difference
     */

    /* ── Bind to port ───────────────────────────────────────────────────── */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[Error] bind() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("[Setup] Bound to port %d\n", PORT);

    /* ── Listen ─────────────────────────────────────────────────────────── */
    if (listen(server_fd, BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr, "[Error] listen() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("[Setup] Listening...\n");

    printf("\n[Ready] http://localhost:%d\n", PORT);
    printf("[Ready] Serving files from current directory\n");
    printf("[Ready] Press Ctrl+C to stop\n");
    printf("------------------------------------------\n");

    /* ── Accept loop — identical logic to Linux version ─────────────────── */
    while (1) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        SOCKET client_fd = accept(server_fd,
                                  (struct sockaddr *)&client_addr,
                                  &addr_len);

        if (client_fd == INVALID_SOCKET) {
            fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            continue;
        }

        handle_client(client_fd, client_addr);
    }

    /* Cleanup (reached only if loop somehow breaks) */
    closesocket(server_fd);
    WSACleanup();   /* ← Windows ONLY: release Winsock resources */
    return 0;
}
