#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 65432
#define BUFFER_SIZE 1024

std::atomic<bool> is_running(true);

// Background thread listening for messages from the server
void receive_handler(int sock_fd) {
    char buffer[BUFFER_SIZE];
    while (is_running) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            if (is_running) {
                std::cout << "\n[!] Disconnected from server." << std::endl;
                is_running = false;
            }
            break;
        }
        std::cout << buffer << std::flush;
    }
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

    char buffer[BUFFER_SIZE];

    // 1. Handshake & Username negotiation
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            std::cout << "[!] Server closed the connection." << std::endl;
            close(sock_fd);
            return 1;
        }

        std::string signal(buffer);
        if (signal == "ENTER_USERNAME") {
            std::cout << "Enter your username: ";
            std::string username;
            std::getline(std::cin, username);
            send(sock_fd, username.c_str(), username.length(), 0);
        } else if (signal == "USERNAME_TAKEN") {
            std::cout << "[!] Username already taken or invalid. Try another." << std::endl;
        } else if (signal == "USERNAME_ACCEPTED") {
            std::cout << "[+] Connected successfully! Type '/quit' to exit.\n" << std::endl;
            break;
        }
    }

    // 2. Start listener thread
    std::thread receiver_thread(receive_handler, sock_fd);
    receiver_thread.detach();

    // 3. User Input Loop
    std::string input;
    while (is_running && std::getline(std::cin, input)) {
        if (!is_running) break;
        if (input == "/quit") {
            send(sock_fd, input.c_str(), input.length(), 0);
            is_running = false;
            break;
        }
        if (!input.empty()) {
            if (send(sock_fd, input.c_str(), input.length(), 0) <= 0) {
                break;
            }
        }
    }

    is_running = false;
    close(sock_fd);
    std::cout << "[*] Chat session closed." << std::endl;
    return 0;
}
