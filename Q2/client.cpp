#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 65433
#define BUFFER_SIZE 65536 // Larger buffer for handling big factorial results

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock_fd);
        return 1;
    }

    std::cout << "[+] Connected to Factorial Server. Type a number or 'quit' to exit.\n";

    char buffer[BUFFER_SIZE];
    std::string input;

    while (true) {
        std::cout << "\nEnter number: ";
        if (!std::getline(std::cin, input)) break;

        if (input.empty()) continue;

        // Send number to server
        if (send(sock_fd, input.c_str(), input.length(), 0) <= 0) {
            std::cout << "[!] Connection lost." << std::endl;
            break;
        }

        if (input == "quit" || input == "exit") {
            std::cout << "[*] Exiting..." << std::endl;
            break;
        }

        // Receive computation result
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            std::cout << "[!] Server closed connection." << std::endl;
            break;
        }

        std::cout << buffer;
    }

    close(sock_fd);
    return 0;
}
