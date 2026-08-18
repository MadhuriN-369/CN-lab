#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 65439
#define BUFFER_SIZE 4096

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Quiz Server failed");
        close(sock_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE];

    // Read username prompt
    memset(buffer, 0, BUFFER_SIZE);
    if (recv(sock_fd, buffer, BUFFER_SIZE - 1, 0) <= 0) {
        close(sock_fd);
        return 1;
    }

    std::cout << "Enter your participant name: ";
    std::string username;
    std::getline(std::cin, username);
    send(sock_fd, username.c_str(), username.length(), 0);

    // Question & Answer Loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        std::string server_msg(buffer);
        std::cout << server_msg << std::flush;

        // Check if server is prompting for an answer
        if (server_msg.find("Your Answer") != std::string::npos) {
            std::string answer;
            std::getline(std::cin, answer);
            send(sock_fd, answer.c_str(), answer.length(), 0);
        }
    }

    close(sock_fd);
    std::cout << "\n[*] Session ended." << std::endl;
    return 0;
}
