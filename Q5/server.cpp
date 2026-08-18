#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 65436
#define BUFFER_SIZE 4096

// Service 1: Calculator
std::string service_calculator(const std::string& query) {
    std::stringstream ss(query);
    double num1, num2;
    char op;
    if (!(ss >> num1 >> op >> num2)) {
        return "ERROR: Invalid format. Usage: <num1> <op (+,-,*,/)> <num2>\n";
    }
    double result = 0;
    if (op == '+') result = num1 + num2;
    else if (op == '-') result = num1 - num2;
    else if (op == '*') result = num1 * num2;
    else if (op == '/') {
        if (num2 == 0) return "ERROR: Division by zero.\n";
        result = num1 / num2;
    } else {
        return "ERROR: Unsupported operator. Allowed: +, -, *, /\n";
    }
    return "Result: " + std::to_string(result) + "\n";
}

// Service 2: String Operations
std::string service_string_ops(const std::string& query) {
    std::stringstream ss(query);
    std::string command;
    ss >> command;
    
    std::string text;
    std::getline(ss, text);
    if (!text.empty() && text[0] == ' ') text = text.substr(1);

    if (command == "UPPER") {
        std::transform(text.begin(), text.end(), text.begin(), ::toupper);
        return "Upper: " + text + "\n";
    } else if (command == "LOWER") {
        std::transform(text.begin(), text.end(), text.begin(), ::tolower);
        return "Lower: " + text + "\n";
    } else if (command == "REVERSE") {
        std::reverse(text.begin(), text.end());
        return "Reversed: " + text + "\n";
    } else if (command == "LENGTH") {
        return "Length: " + std::to_string(text.length()) + "\n";
    }
    return "ERROR: Unknown string command. Allowed: UPPER, LOWER, REVERSE, LENGTH\n";
}

// Service 3: File Transfer Handler (Upload & Download)
void service_file_transfer(int client_fd) {
    char header_buf[BUFFER_SIZE] = {0};
    ssize_t bytes = recv(client_fd, header_buf, BUFFER_SIZE - 1, 0);
    if (bytes <= 0) return;

    std::stringstream ss(header_buf);
    std::string action, filename;
    ss >> action >> filename;

    if (action == "UPLOAD") {
        uint64_t file_size = 0;
        ss >> file_size;

        const char* ready_ack = "READY";
        send(client_fd, ready_ack, strlen(ready_ack), 0);

        std::string save_name = "server_storage_" + filename;
        std::ofstream outfile(save_name, std::ios::binary);
        char buffer[BUFFER_SIZE];
        uint64_t total_read = 0;

        while (total_read < file_size) {
            size_t chunk = std::min((uint64_t)BUFFER_SIZE, file_size - total_read);
            ssize_t r = recv(client_fd, buffer, chunk, 0);
            if (r <= 0) break;
            outfile.write(buffer, r);
            total_read += r;
        }
        outfile.close();

        const char* ok_msg = "SUCCESS: File uploaded.\n";
        send(client_fd, ok_msg, strlen(ok_msg), 0);
    } 
    else if (action == "DOWNLOAD") {
        std::ifstream infile(filename, std::ios::binary | std::ios::ate);
        if (!infile.is_open()) {
            std::string err = "ERR: File not found.\n";
            send(client_fd, err.c_str(), err.length(), 0);
            return;
        }

        uint64_t file_size = infile.tellg();
        infile.seekg(0, std::ios::beg);

        std::string header = "OK " + std::to_string(file_size);
        send(client_fd, header.c_str(), header.length(), 0);

        char ack[10] = {0};
        recv(client_fd, ack, sizeof(ack), 0); // Wait for client READY

        char buffer[BUFFER_SIZE];
        while (infile.read(buffer, BUFFER_SIZE) || infile.gcount() > 0) {
            send(client_fd, buffer, infile.gcount(), 0);
        }
        infile.close();
    }
}

// Service 4: Time Service
std::string service_time() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "Current Server Time: " << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "\n";
    return ss.str();
}

// Client Session Handler
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        std::string request(buffer);
        if (request == "EXIT") break;

        if (request.rfind("CALC ", 0) == 0) {
            std::string resp = service_calculator(request.substr(5));
            send(client_fd, resp.c_str(), resp.length(), 0);
        } 
        else if (request.rfind("STR ", 0) == 0) {
            std::string resp = service_string_ops(request.substr(4));
            send(client_fd, resp.c_str(), resp.length(), 0);
        } 
        else if (request == "TIME") {
            std::string resp = service_time();
            send(client_fd, resp.c_str(), resp.length(), 0);
        } 
        else if (request == "FILE") {
            const char* ready = "READY_FOR_FILE_CMD";
            send(client_fd, ready, strlen(ready), 0);
            service_file_transfer(client_fd);
        } 
        else {
            std::string err = "ERROR: Invalid Service Selected.\n";
            send(client_fd, err.c_str(), err.length(), 0);
        }
    }
    close(client_fd);
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);
    std::cout << "[*] Multi-Service Server active on port " << PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd >= 0) {
            std::thread(handle_client, client_fd).detach();
        }
    }
    close(server_fd);
    return 0;
}
