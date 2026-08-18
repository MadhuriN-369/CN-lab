#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65432
#define BUFFER_SIZE 1024

std::unordered_map<int, std::string> clients; // {socket_fd: username}
std::mutex clients_mutex;

// Trim whitespace and newline characters
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Broadcast message to all connected clients except the sender
void broadcast_message(const std::string& message, int sender_fd = -1) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto& pair : clients) {
        int client_fd = pair.first;
        if (client_fd != sender_fd) {
            ssize_t sent = send(client_fd, message.c_str(), message.length(), MSG_NOSIGNAL);
            (void)sent; // Handled gracefully during client receive/cleanup
        }
    }
}

// Remove client, notify others, and clean up socket
void remove_client(int client_fd) {
    std::string username = "";
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        if (clients.find(client_fd) != clients.end()) {
            username = clients[client_fd];
            clients.erase(client_fd);
        }
    }
    close(client_fd);

    if (!username.empty()) {
        std::cout << "[-] " << username << " disconnected." << std::endl;
        std::string leave_msg = "[SYSTEM] " + username + " has left the chat.\n";
        broadcast_message(leave_msg, -1);
    }
}

// Client handler running on a dedicated thread
void handle_client(int client_fd, sockaddr_in client_addr) {
    char buffer[BUFFER_SIZE];
    std::string username = "";

    // 1. Username Registration & Validation
    while (true) {
        const char* prompt = "ENTER_USERNAME";
        send(client_fd, prompt, strlen(prompt), MSG_NOSIGNAL);

        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            close(client_fd);
            return;
        }

        std::string entered_name = trim(std::string(buffer));
        if (entered_name.empty()) continue;

        std::transform(entered_name.begin(), entered_name.end(), entered_name.begin(), ::toupper);
        bool taken = false;

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            if (entered_name == "SYSTEM") {
                taken = true;
            } else {
                for (const auto& pair : clients) {
                    std::string existing_user = pair.second;
                    std::transform(existing_user.begin(), existing_user.end(), existing_user.begin(), ::toupper);
                    if (existing_user == entered_name) {
                        taken = true;
                        break;
                    }
                }
            }

            if (!taken) {
                username = trim(std::string(buffer));
                clients[client_fd] = username;
            }
        }

        if (taken) {
            const char* taken_msg = "USERNAME_TAKEN";
            send(client_fd, taken_msg, strlen(taken_msg), MSG_NOSIGNAL);
        } else {
            const char* ok_msg = "USERNAME_ACCEPTED";
            send(client_fd, ok_msg, strlen(ok_msg), MSG_NOSIGNAL);
            break;
        }
    }

    std::cout << "[+] " << username << " connected from " 
              << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << std::endl;
    
    std::string join_msg = "[SYSTEM] " + username + " has joined the chat!\n";
    broadcast_message(join_msg, client_fd);

    // 2. Chat Loop
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) {
            break; // Connection closed or error
        }

        std::string msg = trim(std::string(buffer));
        if (msg == "/quit") {
            break;
        }

        if (!msg.empty()) {
            std::string formatted_msg = "[" + username + "]: " + msg + "\n";
            broadcast_message(formatted_msg, client_fd);
        }
    }

    remove_client(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Chat Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Accept error");
            continue;
        }

        std::thread(handle_client, client_fd, client_addr).detach();
    }

    close(server_fd);
    return 0;
}
