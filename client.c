#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 8080
#define BUFFER_SIZE 1024

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

int main(void)
{
    WSADATA wsaData;
    SOCKET clientSocket;
    struct sockaddr_in serverAddr;
    char filename[256];
    char buffer[BUFFER_SIZE];
    int bytesReceived;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Winsock initialization failed.\n");
        return 1;
    }

    //create socket
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (clientSocket == INVALID_SOCKET) {
        printf("Error creating socket.\n");
        WSACleanup();
        return 1;
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    //connect to the server
    if (connect(clientSocket,
                (struct sockaddr *)&serverAddr,
                sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Server connection failed.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    //ask user for file
    printf("Enter filename: ");

    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    filename[strcspn(filename, "\r\n")] = '\0';

    //send file
    uint32_t filenameLength = (uint32_t)strlen(filename);
    uint32_t networkLength = htonl(filenameLength);

    if (!sendAll(clientSocket, (const char *)&networkLength,
                  (int)sizeof(networkLength)) ||
        !sendAll(clientSocket, filename, (int)filenameLength)) {
        printf("Failed to send file.\n");
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    //recieve file content
    while ((bytesReceived = recv(clientSocket, buffer,
                                 sizeof(buffer), 0)) > 0) {
        fwrite(buffer, 1, (size_t)bytesReceived, stdout);
    }

    if (bytesReceived == SOCKET_ERROR) {
        printf("\nError recieving file.\n");
    }

    //close connection
    closesocket(clientSocket);
    WSACleanup();

    return bytesReceived == SOCKET_ERROR ? 1 : 0;
}
