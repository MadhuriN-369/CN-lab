#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65433
#define BUFFER_SIZE 2048

// Helper to compute large factorials as a string to prevent integer overflow
std::string compute_large_factorial(int n) {
    if (n < 0) return "ERROR: Factorial of negative numbers is undefined.";
    if (n > 1000) return "ERROR: Input exceeds server calculation limit (max: 1000).";
    if (n == 0 || n == 1) return "1";

    std::vector<int> result;
    result.push_back(1);

    for (int multiplier = 2; multiplier <= n; ++multiplier) {
        int carry = 0;
        for (size_t i = 0; i < result.size(); ++i) {
            long long prod = static_cast<long long>(result[i]) * multiplier + carry;
            result[i] = prod % 10;
            carry = prod / 10;
        }
        while (carry > 0) {
            result.push_back(carry % 10);
            carry /= 10;
        }
    }

    std::string fact_str = "";
    for (auto it = result.rbegin(); it != result.rend(); ++it) {
        fact_str += std::to_string(*it);
    }
    return fact_str;
}

// Trim whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Client worker thread
void handle_client(int client_fd, sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);
    std::cout << "[+] Worker thread started for client: " << client_ip << ":" << client_port << std::endl;

    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_read = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_read <= 0) {
            // Client closed connection or error occurred
            break;
        }

        std::string input = trim(std::string(buffer));
        if (input == "exit" || input == "quit") {
            break;
        }

        std::string response;
        try {
            // Validate if input is a valid integer
            bool is_valid = !input.empty();
            size_t start = 0;
            if (input[0] == '-') start = 1;
            
            for (size_t i = start; i < input.length(); ++i) {
                if (!std::isdigit(input[i])) {
                    is_valid = false;
                    break;
                }
            }

            if (!is_valid) {
                response = "ERROR: Invalid input. Please send a valid non-negative integer or 'quit'.\n";
            } else {
                int num = std::stoi(input);
                if (num < 0) {
                    response = "ERROR: Negative numbers are not allowed.\n";
                } else {
                    std::string fact_val = compute_large_factorial(num);
                    response = "Result (" + std::to_string(num) + "!): " + fact_val + "\n";
                }
            }
        } catch (const std::out_of_range&) {
            response = "ERROR: Input number is too large to process.\n";
        } catch (...) {
            response = "ERROR: Failed to process request.\n";
        }

        // Send back response
        send(client_fd, response.c_str(), response.length(), MSG_NOSIGNAL);
    }

    std::cout << "[-] Client disconnected: " << client_ip << ":" << client_port << std::endl;
    close(client_fd);
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

    if (listen(server_fd, 20) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Concurrent Factorial Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Accept error");
            continue;
        }

        // Spawn a detached worker thread for each incoming client
        std::thread(handle_client, client_fd, client_addr).detach();
    }

    close(server_fd);
    return 0;
}
