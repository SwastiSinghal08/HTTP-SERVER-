# HTTP-SERVER-

# Mini HTTP Server (Windows - C)

![C](https://img.shields.io/badge/Language-C-blue.svg)
![Platform](https://img.shields.io/badge/Platform-Windows-green.svg)
![Winsock](https://img.shields.io/badge/Networking-Winsock2-orange.svg)
![Status](https://img.shields.io/badge/Status-Active-success.svg)
![License](https://img.shields.io/badge/License-Educational-lightgrey.svg)

>  A lightweight HTTP server built in C using the Winsock API to demonstrate low-level network programming and how web servers work internally.


## Features

 Handles HTTP GET requests  
 MIME type detection  
 Error handling (404, 400)  
 Lightweight & beginner-friendly  
 Runs on Windows using Winsock  

## Tech Stack

| Component  | Technology       |
|------------|------------------|
| Language   | C                |
| Networking | Winsock2 API     |
| Compiler   | GCC / MinGW      |

## Installation & Setup

###  Compile

```bash
gcc -o server http_server_windows.c -lws2_32 -Wall
