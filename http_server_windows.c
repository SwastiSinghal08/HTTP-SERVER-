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
#define MAX_FILE_SIZE (1024 * 1024 * 10) // max file allowed in 10MB

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
            return MIME_TABLE[i].mime_type;
        }
    }
    return "application/octet-stream"; //generic MIME type when no matching found
}

/* 
  parse_request_path — same as Linux version, no changes needed
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
     
    /*
    *This section builds and send a 404 Not Found HTTP response.
    *Creates-response buffer-store html reply
    *-Uses snprintf()-format HTTP response
             Status line: HTTP/1.0 404 Not Found
             content type,length
    */
    char response[1024];
    int  body_len = strlen(body);
    snprintf(response, sizeof(response),
        "HTTP/1.0 404 Not Found\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"                         //blank line to separate headers and body
        "%s",
        body_len, body);

    send(client_fd, response, strlen(response), 0);     //transmit the response to client via socket
    printf("  [Response] 404 Not Found - %s\n", path);  // logs the 404 error on server console for debugging
}

/* ═══════════════════════════════════════════════════════════════════════════
 * send_400 — same, just SOCKET type instead of int
 * ═══════════════════════════════════════════════════════════════════════════ */
 
/*sends HTTP 400 Bad Request response with headers and simple HTML body
 *send_400()-called when HTTP Request is invalid
              GET /index.html HTTP/1.1   (correct form)
*/

void send_400(SOCKET client_fd) {                     //sends a 400 Bad request response to client
    const char *response =                            //Directly defines HTTP response
        "HTTP/1.0 400 Bad Request\r\n"                //Status line
        "Content-Type: text/html\r\n"                 //HTML response
        "Connection: close\r\n"
        "\r\n"
        "<html><body><h1>400 Bad Request</h1></body></html>";   //Simple error page

    send(client_fd, response, strlen(response), 0);         //sends response to client via socket
    printf("  [Response] 400 Bad Request\n");               //logs output
}

/* ═══════════════════════════════════════════════════════════════════════════
 * serve_file — same logic; only SOCKET type and closesocket() differ
 * ═══════════════════════════════════════════════════════════════════════════ */


/* 
    Sends requested file to browser
    This section ensures only valid ,safe and existing files are processed
     before sending them to the client 
*/

void serve_file(SOCKET client_fd, const char *path) {
    const char *local_path = path + 1;                    //removes leading '/' from the path to get actual file name
    printf("  [File]     Serving: %s\n", local_path);     //prints the file serve

    /* Check if file exists
       if not found-send 404 response and exit    
    */
    struct stat file_info;                                //stores file detail(size,existence)
    if (stat(local_path, &file_info) != 0) {              //check if file exists
        send_404(client_fd, path);                        //if not
        return;
    }

    /* Open file in binary mode 
       if file opening fails send 404 response
    */
    FILE *fp = fopen(local_path, "rb");                  //opens file in binary mode
    if (fp == NULL) {                                    //if file cannot open
        send_404(client_fd, path);
        return;
    }

    long file_size = file_info.st_size;                  //get file size
    
    /* Check if file exceeds MAX_FILE_SIZE
       if too large- close file and send 404
    */
    if (file_size > MAX_FILE_SIZE) {                     //prevents very large file(>10MB)
        fclose(fp);                                      //file closes
        send_404(client_fd, path);
        return;
    }

    /* Allocate memory using malloc() to store file content
       if allocation fails-closes file and send 404
    */
    char *file_buf = (char *)malloc(file_size);          //allocate memory to store file
    if (file_buf == NULL) {                              //if memory allocation fail
        fclose(fp);
        send_404(client_fd, path);
        return;
    }

    size_t bytes_read = fread(file_buf, 1, file_size, fp);     //read file into buffer
    fclose(fp);                                                //file closes

    if ((long)bytes_read != file_size) {                       //if reading fails-free memory+404
        free(file_buf);
        send_404(client_fd, path);
        return;
    }

    /* Build and send headers */
    const char *mime = get_mime_type(local_path);            //finds file type(HTML, PNG, etc.)
    char headers[512];                                       //stores HTTP header
    int  header_len = snprintf(headers, sizeof(headers),     //creates reponse header
        "HTTP/1.0 200 OK\r\n"                                //success response
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime, file_size);

    send(client_fd, headers, header_len, 0);                 //sends headers
    send(client_fd, file_buf, file_size, 0);                 //sends file

    printf("  [Response] 200 OK - %s (%ld bytes, %s)\n",
           local_path, file_size, mime);

    free(file_buf);
}

/*
           *this function receive the HTTP request fro the client, extracts the method and path, valiadate 
           the request, and if valaid serves the requested file.
           *client_fd --> socket used to communicate with client, tell from which connection data should be received
           *client_addr --> stores client IP addr and port
           *memset()--> function used to fill a block of memory with a specific value(0 here)
           *method = 'GET'
           *path = '/index.html'   in (GET /index.html HTTP/1.1)
*/
void handle_client(SOCKET client_fd, struct sockaddr_in client_addr) {
    char request_buf[BUFFER_SIZE];                         //create buffer to store HTTP request

    printf("\n[Connection] %s:%d\n",                       //print client IP and port
           inet_ntoa(client_addr.sin_addr),                //converts IP to readable format
           ntohs(client_addr.sin_port));                   //converts port number 

    memset(request_buf, 0, sizeof(request_buf));               //fills buffer with 0 and remove garbage values
    int bytes_received = recv(client_fd, request_buf,          //receive HTTP request from client and stores it in buffer 
                              sizeof(request_buf) - 1, 0);     //-1 leaves space for null character

    if (bytes_received <= 0) {                                 //check if request failed or empty
        closesocket(client_fd);                                //connection closes
        return;
    }

    request_buf[bytes_received] = '\0';                       //converts buffer into proper string

    char first_line[256] = {0};                               //buffer for the first line
    sscanf(request_buf, "%255[^\r\n]", first_line);           //extract first line until newline
    printf("  [Request]  %s\n", first_line);                  //printf request line

    char method[16]         = {0};                            //for method
    char path[MAX_PATH_LEN] = {0};                            //for file path

    if (!parse_request_path(request_buf, method, path)) {      //extracts method and path
        send_400(client_fd);                                   
        closesocket(client_fd);
        return;
    }

    if (strcmp(method, "GET") != 0) {                                   //compare method with GET
        printf("  [Ignored]  Method not supported: %s\n", method);      //logs unsupported method
        send_400(client_fd);                                            //sends error and exit
        closesocket(client_fd);
        return;
    }

    serve_file(client_fd, path);                     //calls function to send requested file

    closesocket(client_fd);                          //closes client socket
}



/*  
              *MAKEWORD(2,2) --> version 2.2

*/

int main(void) {

    printf("========================================\n");          //displays server title just for clarity
    printf("   mini_http_server  (Windows / Winsock)\n");
    printf("========================================\n\n");

    
    WSADATA wsa_data;                                             //structure to store socket lib info
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);       //starts windows socket system
    if (wsa_result != 0) {                                        //checks if initialization failed
        fprintf(stderr, "[Error] WSAStartup failed: %d\n", wsa_result);
        return 1;
    }
    printf("[Setup] Winsock initialized\n");

   
    /*           Sets server address, binds socket to port, and start listening for clients
                 create TCP socket
                 AF_INET --> IPv4
                 SOCK_STREAM --> TCP
    */
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {                                 //check if socket creation failed
        fprintf(stderr, "[Error] socket() failed: %d\n", WSAGetLastError());
        WSACleanup();                                                  //cleans resources
        return 1;
    }
    printf("[Setup] Socket created\n");                         //confirms socket created

    
    int opt = 1;                                
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,         //allow reuse of port avoids 'address already in use' error
               (const char *)&opt, sizeof(opt));
    
    struct sockaddr_in server_addr;                        //structure to store server IP and port
    memset(&server_addr, 0, sizeof(server_addr));          //removes garbage values
    server_addr.sin_family      = AF_INET;                  // sets adress family specifically IPv4 protocol
    server_addr.sin_addr.s_addr = INADDR_ANY;              //accepts connection from any IP address
    server_addr.sin_port        = htons(PORT);             //set port 8080 , htons() converts to network format

    if (bind(server_fd, (struct sockaddr *)&server_addr,        //link socket with IP+port 
             sizeof(server_addr)) == SOCKET_ERROR) {
        fprintf(stderr, "[Error] bind() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("[Setup] Bound to port %d\n", PORT);           //confirms successful binding

    

    /*
                 *starts listening for clients
                *BACKLOG --> max waiting client
    */
    if (listen(server_fd, BACKLOG) == SOCKET_ERROR) {
        fprintf(stderr, "[Error] listen() failed: %d\n", WSAGetLastError());
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }
    printf("[Setup] Listening...\n");          //Indicates server has started litening

    printf("\n[Ready] http://localhost:%d\n", PORT);               //shows url to open in browser
    printf("[Ready] Serving files from current directory\n");
    printf("[Ready] Press Ctrl+C to stop\n");
    printf("------------------------------------------\n");

    /*
             *This part continuously accepts client connection using an infinite loop.
             *For each client, it creates a new socket, process the request using handle_client, and 
              keeps the server running for multiple client 
    */
    while (1) {
        struct sockaddr_in client_addr;               //stores client IP and port
        int addr_len = sizeof(client_addr);           //size of address structure

        SOCKET client_fd = accept(server_fd,
                                  (struct sockaddr *)&client_addr,
                                  &addr_len);         //waits for client request, accept connection, create new socket for the client



        if (client_fd == INVALID_SOCKET) {            //check if accept failed
            fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            continue;
        }

        handle_client(client_fd, client_addr);           //calls function to read request, process it, send response
    }

    
    closesocket(server_fd);                           //closes server socket
    WSACleanup();                                     //releases winsock resources
    return 0;
}
