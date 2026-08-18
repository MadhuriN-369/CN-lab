#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>
#include <thread>
#include <cstring>
#include <cstdio>
#include <memory>
#include <array>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65440
#define BUFFER_SIZE 4096

// Permitted Linux Commands Whitelist
const std::unordered_set<std::string> ALLOWED_COMMANDS = {
    "ls", "pwd", "date", "whoami", "uptime", "uname", "df", "free", "id"
};

// Forbidden shell chaining and redirection characters
const std::string FORBIDDEN_CHARS = ";|&`$><\\!{}()[]*?";

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// Input Validation and Security Sanity Check
bool validate_command(const std::string& full_cmd, std::string& error_reason) {
    if (full_cmd.empty()) {
        error_reason = "Empty command received.";
        return false;
    }

    // Check for dangerous shell metacharacters
    for (char ch : full_cmd) {
        if (FORBIDDEN_CHARS.find(ch) != std::string::npos) {
            error_reason = "Security Violation: Prohibited shell character detected ('" + std::string(1, ch) + "').";
            return false;
        }
    }

    // Extract base binary/command name
    std::istringstream ss(full_cmd);
    std::string base_cmd;
    ss >> base_cmd;

    if (ALLOWED_COMMANDS.find(base_cmd) == ALLOWED_COMMANDS.end()) {
        error_reason = "Permission Denied: Command '" + base_cmd + "' is not in the server whitelist.";
        return false;
    }

    return true;
}

// Execute command safely and capture both stdout and stderr
std::string execute_linux_command(const std::string& cmd) {
    std::string command_with_stderr = cmd + " 2>&1";
    std::array<char, 256> buffer;
    std::string result;

    FILE* pipe = popen(command_with_stderr.c_str(), "r");
    if (!pipe) {
        return "ERROR: Failed to spawn process pipe.\n";
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

    int return_code = pclose(pipe);
    if (result.empty()) {
        result = "(Command executed successfully with no output)\n";
    }

    result += "\n[Process Exit Code: " + std::to_string(WEXITSTATUS(return_code)) + "]\n";
    return result;
}

void handle_client(int client_fd, sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    std::cout << "[+] Client connected from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        std::string raw_cmd = trim(std::string(buffer));
        if (raw_cmd == "exit" || raw_cmd == "quit") {
            break;
        }

        std::string response;
        std::string error_reason;

        // Perform security check before execution
        if (!validate_command(raw_cmd, error_reason)) {
            response = "[SECURITY REJECTION] " + error_reason + "\n";
            std::cout << " [!] Blocked request from " << client_ip << ": '" << raw_cmd 
                      << "' -> Reason: " << error_reason << std::endl;
        } else {
            std::cout << " [*] Executing: '" << raw_cmd << "' for " << client_ip << std::endl;
            response = execute_linux_command(raw_cmd);
        }

        // Send payload length (4 bytes uint32_t network endian) followed by output
        uint32_t resp_len = htonl(response.length());
        send(client_fd, (char*)&resp_len, sizeof(resp_len), MSG_NOSIGNAL);
        send(client_fd, response.c_str(), response.length(), MSG_NOSIGNAL);
    }

    std::cout << "[-] Client disconnected: " << client_ip << std::endl;
    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
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

    std::cout << "[*] Remote Command Execution Server running on port " << PORT << "..." << std::endl;
    std::cout << "[*] Whitelisted Commands: ls, pwd, date, whoami, uptime, uname, df, free, id" << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd, client_addr).detach();
        }
    }

    close(server_fd);
    return 0;
}
