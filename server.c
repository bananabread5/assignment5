
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define PORT 8080
#define BUFFER_SIZE 256
#define max_filename 255
#define max_file_size 255
#define timeout_ms 5000

volatile LONG shutdownRequested = 0;

//Bonus feature Ctrl + C
BOOL WINAPI consoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
        InterlockedExchange(&shutdownRequested, 1);
        return TRUE;
    }

    return FALSE;
}

//send all data, even if only parts of it
int sendAll(SOCKET clientSocket, const char *data, int length)
{
    int totalSent = 0;

    while (totalSent < length) {
        int sent = send(clientSocket, data + totalSent,
                        length - totalSent, 0);

        if (sent == SOCKET_ERROR || sent == 0) {
            return 0;
        }

        totalSent += sent;
    }

    return 1;
}

int recvExact(SOCKET clientSocket, char *data, int length)
{
    int totalReceived = 0;

    while (totalReceived < length) {
        int received = recv(clientSocket,
                            data + totalReceived,
                            length - totalReceived, 0);

        if (received <= 0) {
            return 0;
        }

        totalReceived += received;
    }

    return 1;
}

//send error to client
void sendError(SOCKET clientSocket, const char *message)
{
    sendAll(clientSocket, message, (int)strlen(message));
}

//take simple filenames only
int validFilename(const char *filename)
{
    if (filename[0] == '\0' ||
        strcmp(filename, ".") == 0 ||
        strcmp(filename, "..") == 0) {
        return 0;
    }

    for (int i = 0; filename[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)filename[i];

        if (ch < 32 || ch == 127 ||
            ch == '/' || ch == '\\' || ch == ':') {
            return 0;
        }
    }

    return 1;
}

int main(void)
{
    WSADATA wsaData;
    SOCKET serverSocket;
    struct sockaddr_in serverAddr;
    char filename[max_filename + 1];
    char buffer[BUFFER_SIZE];

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Error initalizing winstock.\n");
        return 1;
    }

    //register Ctrl+C handler
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        printf("Error while registering shutdown.\n");
        WSACleanup();
        return 1;
    }

    //create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket == INVALID_SOCKET) {
        printf("Error creating socket.\n");
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        WSACleanup();
        return 1;
    }

    //allow port to be reused
    BOOL reuseAddress = TRUE;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR,
               (const char *)&reuseAddress, sizeof(reuseAddress));

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSocket, (struct sockaddr *)&serverAddr,
             sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Error while binding to port %d.\n", PORT);
        closesocket(serverSocket);
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, 5) == SOCKET_ERROR) {
        printf("Error while listening for connections.\n");
        closesocket(serverSocket);
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        WSACleanup();
        return 1;
    }

    u_long nonblocking = 1;

    if (ioctlsocket(serverSocket, FIONBIO, &nonblocking)
        == SOCKET_ERROR) {
        printf("Error while configuring server socket.\n");
        closesocket(serverSocket);
        SetConsoleCtrlHandler(consoleHandler, FALSE);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %d.\n", PORT);
    printf("Files must be in the files directory.\n");
    printf("Press Ctrl+C to shut down the server.\n");

    while (InterlockedCompareExchange(
               &shutdownRequested, 0, 0) == 0) {

        struct sockaddr_in clientAddr;
        int clientSize = sizeof(clientAddr);

        SOCKET clientSocket = accept(
            serverSocket,
            (struct sockaddr *)&clientAddr,
            &clientSize
        );

        if (clientSocket == INVALID_SOCKET) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) {
                if (InterlockedCompareExchange(
                        &shutdownRequested, 0, 0) == 0) {
                    printf("Warning: Accept failed.\n");
                }
            }

            Sleep(50);
            continue;
        }

        //configure the accepted socket 
        u_long blocking = 0;

        if (ioctlsocket(clientSocket, FIONBIO, &blocking)
            == SOCKET_ERROR) {
            closesocket(clientSocket);
            continue;
        }

        DWORD timeout = timeout_ms;

        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO,
                   (const char *)&timeout, sizeof(timeout));

        setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO,
                   (const char *)&timeout, sizeof(timeout));

        printf("Client connected.\n");

        //read filename through null terminator
        uint32_t networkLength;
        uint32_t filenameLength;

        if (!recvExact(clientSocket,
                       (char *)&networkLength,
                       (int)sizeof(networkLength))) {
            sendError(clientSocket,
                      "ERROR: Could not receive filename length.\n");
            goto close_client;
        }

        filenameLength = ntohl(networkLength);

        if (filenameLength == 0 ||
            filenameLength > max_filename) {
            sendError(clientSocket,
                      "ERROR: Invalid filename length.\n");
            goto close_client;
        }

        if (!recvExact(clientSocket, filename,
                       (int)filenameLength)) {
            sendError(clientSocket,
                      "ERROR: Incomplete filename.\n");
            goto close_client;
        }

        if (memchr(filename, '\0', filenameLength) != NULL) {
            sendError(clientSocket,
                      "ERROR: Null byte in filename.\n");
            goto close_client;
        }

        filename[filenameLength] = '\0';

        if (!validFilename(filename)) {
            sendError(clientSocket, "Invalid filename.\n");
            goto close_client;
        }

        //build a path in the designated directory
        char filepath[300];

        int pathLength = snprintf(filepath, sizeof(filepath),
                                  "files\\%s", filename);

        if (pathLength < 0 ||
            pathLength >= sizeof(filepath)) {
            sendError(clientSocket, "Filename too long.\n");
            goto close_client;
        }

        FILE *file = fopen(filepath, "rb");

        if (file == NULL) {
            sendError(clientSocket,
                      "File not found.\n");
            goto close_client;
        }

        //detect files over byte limit
        size_t bytesRead = fread(buffer, 1, BUFFER_SIZE, file);

        if (ferror(file)) {
            fclose(file);
            sendError(clientSocket,
                      "ERROR while reading file.\n");
            goto close_client;
        }

        if (bytesRead > max_file_size) {
            fclose(file);
            sendError(clientSocket,
                      "File exceeds the 255-byte limit.\n");
            goto close_client;
        }

        fclose(file);

        //send file after size validation
        if (!sendAll(clientSocket, buffer, (int)bytesRead)) {
            printf("Failure to send file contents.\n");
        } else {
            printf("File sent: %s (%zu bytes).\n",
                   filename, bytesRead);
        }

close_client:
        shutdown(clientSocket, SD_BOTH);
        closesocket(clientSocket);
        printf("Client connection closed.\n");
    }

    //server shutdown
    printf("\nShutting down server...\n");
    closesocket(serverSocket);
    SetConsoleCtrlHandler(consoleHandler, FALSE);
    WSACleanup();
    printf("Server sockets closed. Shutdown complete.\n");

    return 0;
}
