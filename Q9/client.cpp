#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 65440
#define BUFFER_SIZE 4096

// Helper to reliably read exact bytes across TCP chunks
bool read_exact(int sock, char* buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t b = recv(sock, buffer + total, size - total, 0);
        if (b <= 0) return false;
        total += b;
    }
    return true;
}

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
        perror("Connection to Server failed");
        close(sock_fd);
        return 1;
    }

    std::cout << "===================================================\n";
    std::cout << "  REMOTE COMMAND EXECUTION CLIENT (Whitelisted)    \n";
    std::cout << "  Supported: ls, pwd, date, whoami, uptime, uname, df, free, id\n";
    std::cout << "  Type 'exit' or 'quit' to close connection.       \n";
    std::cout << "===================================================\n";

    std::string input;
    while (true) {
        std::cout << "\nrce-shell> ";
        if (!std::getline(std::cin, input)) break;
        if (input.empty()) continue;

        if (send(sock_fd, input.c_str(), input.length(), 0) <= 0) {
            std::cout << "[!] Connection lost." << std::endl;
            break;
        }

        if (input == "exit" || input == "quit") {
            break;
        }

        // 1. Read Response Length Header
        uint32_t resp_len_net = 0;
        if (!read_exact(sock_fd, (char*)&resp_len_net, sizeof(resp_len_net))) {
            std::cout << "[!] Server disconnected." << std::endl;
            break;
        }
        uint32_t resp_len = ntohl(resp_len_net);

        // 2. Read Complete Output Payload
        std::vector<char> output_buf(resp_len + 1, 0);
        if (!read_exact(sock_fd, output_buf.data(), resp_len)) {
            std::cout << "[!] Error receiving complete command response." << std::endl;
            break;
        }

        std::cout << output_buf.data();
    }

    close(sock_fd);
    std::cout << "[*] Disconnected." << std::endl;
    return 0;
}
