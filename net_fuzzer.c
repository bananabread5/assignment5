#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define BUFFER_SIZE 1024
#define FLOOD_SIZE 10000
#define TIMEOUT_MS 3000

// Connect to the test server
SOCKET connectToServer(void)
{
    SOCKET sock;
    struct sockaddr_in serverAddr;

    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock == INVALID_SOCKET) {
        printf("ERROR: Could not create socket.\n");
        return INVALID_SOCKET;
    }

    DWORD timeout = TIMEOUT_MS;

    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
               (const char *)&timeout, sizeof(timeout));

    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP,
                  &serverAddr.sin_addr) != 1) {
        printf("ERROR: Invalid server address.\n");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    if (connect(sock, (struct sockaddr *)&serverAddr,
                sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("ERROR: Could not connect to server.\n");
        closesocket(sock);
        return INVALID_SOCKET;
    }

    return sock;
}

// Send all bytes, handling partial sends
int sendAll(SOCKET sock, const char *data, int length)
{
    int totalSent = 0;

    while (totalSent < length) {
        int sent = send(sock, data + totalSent,
                        length - totalSent, 0);

        if (sent == SOCKET_ERROR || sent == 0) {
            return 0;
        }

        totalSent += sent;
    }

    return 1;
}

// Display the response returned by the server
void receiveResponse(SOCKET sock)
{
    char buffer[BUFFER_SIZE];
    int received;
    int totalReceived = 0;

    while ((received = recv(sock, buffer,
                            sizeof(buffer), 0)) > 0) {
        int i;

        for (i = 0; i < received; i++) {
            putchar((unsigned char)buffer[i]);
        }

        totalReceived += received;
    }

    if (received == SOCKET_ERROR) {
        int error = WSAGetLastError();

        if (error == WSAETIMEDOUT ||
            error == WSAEWOULDBLOCK) {
            printf("\n[INFO] Receive timed out.\n");
        } else {
            printf("\n[INFO] Socket receive ended with error %d.\n",
                   error);
        }
    } else if (totalReceived == 0) {
        printf("[INFO] Server closed without sending a response.\n");
    }

    printf("\n");
}

// Send a test payload and read the response
void runPayloadTest(const char *testName,
                    const char *payload, int length)
{
    printf("\n========== %s ==========\n", testName);

    SOCKET sock = connectToServer();

    if (sock == INVALID_SOCKET) {
        return;
    }

    printf("Sending %d bytes...\n", length);

    uint32_t networkLength = htonl((uint32_t)length);

    if (sendAll(sock, (const char *)&networkLength,
                (int)sizeof(networkLength)) &&
        sendAll(sock, payload, length)) {
        printf("Payload sent. Server response:\n");
        receiveResponse(sock);
    } else {
        printf("[INFO] Connection ended during transmission.\n");
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
}

// Connect and disconnect without sending data
void testImmediateDisconnect(void)
{
    printf("\n========== Immediate Disconnect ==========\n");

    SOCKET sock = connectToServer();

    if (sock == INVALID_SOCKET) {
        return;
    }

    printf("Connected. Closing without sending a request.\n");
    closesocket(sock);
}

// Send a partial filename, then disconnect
void testMidTransmissionDisconnect(void)
{
    printf("\n========== Mid-Transmission Disconnect ==========\n");

    SOCKET sock = connectToServer();

    if (sock == INVALID_SOCKET) {
        return;
    }

    uint32_t networkLength = htonl(10);
    const char partial[] = "hello";

    if (sendAll(sock, (const char *)&networkLength,
                (int)sizeof(networkLength)) &&
        sendAll(sock, partial, (int)strlen(partial))) {
        printf("Partial filename sent. Disconnecting now.\n");
    }

    shutdown(sock, SD_BOTH);
    closesocket(sock);
}

// Test that a legitimate request still works
void testValidFilename(void)
{
    const char filename[] = "hello.txt";

    runPayloadTest("Valid Filename",
                   filename, (int)strlen(filename));
}

int main(void)
{
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("ERROR: Winsock initialization failed.\n");
        return 1;
    }

    printf("TCP Network Fuzzer\n");
    printf("Target: %s:%d\n", SERVER_IP, PORT);
    printf("Only use this fuzzer against your own test server.\n");

    // Path traversal attacks
    const char traversalUnix[] = "../../etc/passwd";
    runPayloadTest("Unix Path Traversal",
                   traversalUnix, (int)strlen(traversalUnix));

    const char traversalWindows[] = "..\\..\\Windows\\win.ini";
    runPayloadTest("Windows Path Traversal",
                   traversalWindows, (int)strlen(traversalWindows));

    // Absolute paths
    const char absoluteUnix[] = "/etc/passwd";
    runPayloadTest("Unix Absolute Path",
                   absoluteUnix, (int)strlen(absoluteUnix));

    const char absoluteWindows[] = "C:\\Windows\\win.ini";
    runPayloadTest("Windows Absolute Path",
                   absoluteWindows, (int)strlen(absoluteWindows));

    // Send 10,000 bytes without a null terminator
    char *flood = (char *)malloc(FLOOD_SIZE);

    if (flood != NULL) {
        memset(flood, 'A', FLOOD_SIZE);

        runPayloadTest("10,000-Byte Payload Flood",
                       flood, FLOOD_SIZE);

        free(flood);
    } else {
        printf("ERROR: Could not allocate flood buffer.\n");
    }

    // Embedded null byte and malicious trailing data
    const char nullInjection[] =
        "hello.txt\0../../secret.txt";

    runPayloadTest("Null Byte Injection",
                   nullInjection, (int)sizeof(nullInjection) - 1);

    // Empty filename
    runPayloadTest("Empty Filename", "", 0);

    // Connection teardown tests
    testImmediateDisconnect();
    testMidTransmissionDisconnect();

    // Check server recovery
    testValidFilename();

    printf("\nFuzzing completed.\n");

    WSACleanup();
    return 0;
}
