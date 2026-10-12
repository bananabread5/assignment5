# assignment5

Problem Statement & Architecture:
The assignment asked to create a client-server application in C. First, the code receives the requested file, checks whether its valid and then reads and sends the file to the client. The socket design is as follows: server.c connects, verifies/validates request, and sends files. client.c connects to the server, asks for a file, and then generates responses based on the file and its data. Lastly, Net_fuzzer.c preforms tests to see how the code handles certain errors. For protocol choices and management, the server listens on PORT 8080 and the client connects through 127.0.0.1, and uses windows Winstock for communication.

Socket Security & Buffer Analysis:
My code handles packet framing by using sendAll() and recvExact() to handle partial transmissions in order to make sure complete messages are transferred. It handles buffer overload prevention by rejecting all files that are greater than 255 byte. Lastly is filename sanitization which is handled by the validfilename() function, which rejects empty names, invalid characters, path separators, and embedded null bytes.

Pros and cons:
pros the iterative server handles one client at a time which can make the code easier to understand and easier to debug. It also allows for a reduction in shared resource issues. Some cons are the fact that the server is unable to process multiple clients at the same time. Or the fact that all clients share the server process so server failures and issues can affect the whole application. A concurrent multithreaded sever however can handle multiple clients at the same time, meaning it wont run into the same issues as a iterative single-client processing server, but requires more resources to execute. 

AI Fuzzing Reflection: 
For the prompt, I just gave the AI my code and then gave it all the requirements in the AI Network Fuzzer Generation section. It then created a net_fuzzer.c which I used to test my code. Everything worked out except for the insertion of null bytes which was fixed by editing both the server.c and client.c code to search specfically for null injections. If it finds a file with null bytes, it will send out an error message now instead, whereas before it just printed the txt files contents.

