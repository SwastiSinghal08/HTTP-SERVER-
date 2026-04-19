/*
 * http_server_windows.c — Mini HTTP Server (Windows Version)
 * Compile : gcc http_server_windows.c -o http_server_windows.exe -lws2_32
              (the -lws2_32 flag links the Windows socket lib)
 * Run     : http_server_windows.exe
 * Test    : http://localhost:8080 this opens our homepage running on port 8080
 */

/*  Windows Socket Header (replaces all Linux socket headers)  */
#include <winsock2.h>       /* socket, bind, listen, accept, send, recv functions required to
                            implement TCP communication   */
#include <ws2tcpip.h>       /* inet_ntoa, sockaddr_in to handle IP addresses.                   */

/* Standard C Headers */
#include <stdio.h>  // input output function'
#include <stdlib.h> // memory allocation(malloc(),exit())
#include <string.h> // string operation
#include <sys/stat.h> // Used to check file existence and its properties
#include <errno.h>   // helps in error handling , store errors code


#pragma comment(lib, "ws2_32.lib")  /* Tells compiler to link winsock library 
                                     otherwise socket functions wont work */

/* Configuration  */
#define PORT          8080  // Server runs on this port
#define BACKLOG       10    // max no of client waiting if server is busy
#define BUFFER_SIZE   4096  //Size of buffer used to Read request,Send response(4KB enough for http request) 
#define MAX_PATH_LEN  256 // Max length of file path (like /index.html)
#define MAX_FILE_SIZE (1024 * 1024 * 10) // max file allowed is 10MB

/* MIME Type Table for identifying the file type and assigning the correct MIME type. */
typedef struct {              // structure storing file extension and mime type 
    const char *extension;
    const char *mime_type;
} MimeEntry;

static const MimeEntry MIME_TABLE[] = {      // This is array of structure assinging mime type to various extension
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
    { NULL,    NULL                      } // To mark end of MIME table
};

/* Give File name return correct mime type
*/
const char *get_mime_type(const char *filename) {
    for (int i = 0; MIME_TABLE[i].extension != NULL; i++) { // iterates through mime table until NULL
        if (strstr(filename, MIME_TABLE[i].extension) != NULL) { //search substring inside string
            return MIME_TABLE[i].mime_type; //returns a matching MIME type
        }
    }
    return "application/octet-stream"; //generic MIME type when no matching found
}

/* 
  parse_request_path() — same as Linux version, no changes needed
*/
int parse_request_path(const char *request, char *method, char *path) {
    //extracts the HTTP method (GET) and requested path (/index.html) from the request string.
    // EX. GET /index.html HTTP/1.1 so method → "GET" , path → "/index.html"
    if (sscanf(request, "%255s %255s", method, path) != 2) { 
        return 0; // invalid request format
    }
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html"); // "/" means homepage so serve index.html
    }
    return 1;
}
/* 
 Sends a 404 Not Found response to the browser when file is missing.
  */
void send_404(SOCKET client_fd, const char *path) {
    /*
     * SOCKET is a typedef in winsock2.h
     * On Linux we used int; on Windows we use SOCKET
     */
    char body[512]; // stores HTML response
    snprintf(body, sizeof(body),
        "<html><head><title>OOPS - 404 Not Found</title></head>"
        "<body style='font-family:monospace;padding:40px'>"
        "<h1>404 &mdash; Not Found</h1>"
        "<p>The file <code>%s</code> could not be found.</p>"
        "<hr><small>mini_http_server / C (Windows)</small>"
        "</body></html>",
        path); // In %S

    char response[1024]; //full http response
    int  body_len = strlen(body); // length of our html content
    snprintf(response, sizeof(response),
        "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        body_len, body); // it builds http reponse such as status line, headers, body

    send(client_fd, response, strlen(response), 0); // sends response to the browser
    printf("  [Response] 404 Not Found - %s\n", path);
}

// send_400 — same, just SOCKET type instead of int
 
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

//serve_file — same logic; only SOCKET type and closesocket() differ
 
void serve_file(SOCKET client_fd, const char *path) {
    const char *local_path = path + 1; /* skip the leading '/', i.e /index.html → index.html */
    printf("  [File]     Serving: %s\n", local_path);

    /* Check if file exists */
    struct stat file_info; // structure to store file info
    if (stat(local_path, &file_info) != 0) { // to check if file exists
        send_404(client_fd, path);
        return;
    }

    /* Opens file in binary mode */
    FILE *fp = fopen(local_path, "rb");
    if (fp == NULL) {
        send_404(client_fd, path);
        return;
    }

    long file_size = file_info.st_size; // knowing file size

    if (file_size > MAX_FILE_SIZE) {
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    /* Read file into heap buffer */
    char *file_buf = (char *)malloc(file_size); //allocates memory
    if (file_buf == NULL) {
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    size_t bytes_read = fread(file_buf, 1, file_size, fp); //reading file into memory
    fclose(fp);

    if ((long)bytes_read != file_size) {
        free(file_buf);
        send_404(client_fd, path);
        return;
    }

    /* Build and send headers */
    const char *mime = get_mime_type(local_path); //get correct MIME type
    char headers[512];
    int  header_len = snprintf(headers, sizeof(headers), // builds HTTP headers
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_size);

    send(client_fd, headers, header_len, 0); //sends headers and fie content to browser
    send(client_fd, file_buf, file_size, 0);

    printf("  [Response] 200 OK - %s (%ld bytes, %s)\n",
           local_path, file_size, mime);

    free(file_buf); //frees memory after sending
}

 //handle_client — SOCKET type; closesocket() instead of close()
 
void handle_client(SOCKET client_fd, struct sockaddr_in client_addr) { //handles one client at a time
    char request_buf[BUFFER_SIZE];

    printf("\n[Connection] %s:%d\n",
           inet_ntoa(client_addr.sin_addr),
           ntohs(client_addr.sin_port));

    memset(request_buf, 0, sizeof(request_buf));
    int bytes_received = recv(client_fd, request_buf,
                              sizeof(request_buf) - 1, 0); //receive's client request

    if (bytes_received <= 0) {
        closesocket(client_fd);   // Windows uses closesocket(), not close() 
        return;
    }

    request_buf[bytes_received] = '\0'; // converts into string

    char first_line[256] = {0};
    sscanf(request_buf, "%255[^\r\n]", first_line); //extracts first line of request
    printf("  [Request]  %s\n", first_line);

    char method[16]         = {0};
    char path[MAX_PATH_LEN] = {0};

    if (!parse_request_path(request_buf, method, path)) { //extract method + path
        send_400(client_fd);
        closesocket(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) { // server only allows GET method
        printf("  [Ignored]  Method not supported: %s\n", method);
        send_400(client_fd);
        closesocket(client_fd);
        return;
    }

    serve_file(client_fd, path); //serves the required file to the client

    closesocket(client_fd);   // closesocket() instead of close() 
}

 // MAIN FUNCTION — adds WSAStartup (Windows socket init) and WSACleanup at the end
 
int main(void) {

    printf("========================================\n");
    printf("   mini_http_server  (Windows / Winsock)\n");
    printf("========================================\n\n");

    /* WINDOWS ONLY: Initialize Winsock 
     * On Linux, sockets are ready immediately.
     * On Windows, you MUST call WSAStartup() first or nothing works.
     *
     * WSADATA  = a struct that Windows fills with socket library info
     * MAKEWORD(2,2) = request Winsock version 2.2 (the standard modern version)
     */
    WSADATA wsa_data;
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (wsa_result != 0) {
        fprintf(stderr, "[Error] WSAStartup failed: %d\n", wsa_result);
        return 1;
    }
    printf("[Setup] Winsock initialized\n");

    /* Create TCP socket 
     * Same as Linux: AF_INET = IPv4, SOCK_STREAM = TCP
     * On Windows, socket() returns type SOCKET (not int)
     * INVALID_SOCKET is the Windows equivalent of Linux's -1
    */
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        fprintf(stderr, "[Error] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    printf("[Setup] Socket created\n");

    /*SO_REUSEADDR — same as Linux */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&opt, sizeof(opt));
    /*
     * Note: On Windows, setsockopt takes (const char*) not (void*)
     * The cast (const char *)&opt handles this difference
     */

    /*Bind to port */
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

    //Listen
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

    //Accept loop — identical logic to Linux version 
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

    // Cleanup (reached only if loop somehow breaks) 
    closesocket(server_fd);
    WSACleanup();   // Windows ONLY: release Winsock resources 
    return 0;
}
