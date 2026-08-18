#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define PORT 65436
#define BUFFER_SIZE 4096

void handle_file_service(int sock_fd) {
    std::cout << "\n--- File Transfer Menu ---\n";
    std::cout << "1. Upload File\n";
    std::cout << "2. Download File\n";
    std::cout << "Choice: ";
    int fchoice;
    std::cin >> fchoice;
    std::cin.ignore();

    if (fchoice == 1) {
        std::cout << "Enter local filepath to upload: ";
        std::string path;
        std::getline(std::cin, path);

        std::ifstream infile(path, std::ios::binary | std::ios::ate);
        if (!infile.is_open()) {
            std::cout << "[!] Could not open local file.\n";
            return;
        }

        uint64_t file_size = infile.tellg();
        infile.seekg(0, std::ios::beg);

        size_t last_slash = path.find_last_of("/\\");
        std::string filename = (last_slash == std::string::npos) ? path : path.substr(last_slash + 1);

        std::string meta = "UPLOAD " + filename + " " + std::to_string(file_size);
        send(sock_fd, meta.c_str(), meta.length(), 0);

        char ack[16] = {0};
        recv(sock_fd, ack, sizeof(ack) - 1, 0); // Server READY

        char buffer[BUFFER_SIZE];
        while (infile.read(buffer, BUFFER_SIZE) || infile.gcount() > 0) {
            send(sock_fd, buffer, infile.gcount(), 0);
        }
        infile.close();

        char resp[BUFFER_SIZE] = {0};
        recv(sock_fd, resp, BUFFER_SIZE - 1, 0);
        std::cout << "[Server] " << resp;
    } 
    else if (fchoice == 2) {
        std::cout << "Enter remote filename to download: ";
        std::string filename;
        std::getline(std::cin, filename);

        std::string meta = "DOWNLOAD " + filename;
        send(sock_fd, meta.c_str(), meta.length(), 0);

        char resp[BUFFER_SIZE] = {0};
        ssize_t r = recv(sock_fd, resp, BUFFER_SIZE - 1, 0);
        if (r <= 0) return;

        std::stringstream ss(resp);
        std::string status;
        ss >> status;

        if (status != "OK") {
            std::cout << "[Server] " << resp << std::endl;
            return;
        }

        uint64_t file_size;
        ss >> file_size;

        const char* ready = "READY";
        send(sock_fd, ready, strlen(ready), 0);

        std::ofstream outfile("downloaded_" + filename, std::ios::binary);
        char buffer[BUFFER_SIZE];
        uint64_t total_received = 0;

        while (total_received < file_size) {
            size_t chunk = std::min((uint64_t)BUFFER_SIZE, file_size - total_received);
            ssize_t b = recv(sock_fd, buffer, chunk, 0);
            if (b <= 0) break;
            outfile.write(buffer, b);
            total_received += b;
        }
        outfile.close();
        std::cout << "[+] File downloaded successfully as: downloaded_" << filename << "\n";
    }
}

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        return 1;
    }

    char buffer[BUFFER_SIZE];

    while (true) {
        std::cout << "\n==============================\n";
        std::cout << " MULTI-SERVICE CLIENT MENU    \n";
        std::cout << "==============================\n";
        std::cout << "1. Calculator Service\n";
        std::cout << "2. String Operations Service\n";
        std::cout << "3. File Transfer Service\n";
        std::cout << "4. Time Service\n";
        std::cout << "5. Exit\n";
        std::cout << "Enter selection [1-5]: ";

        int choice;
        if (!(std::cin >> choice)) break;
        std::cin.ignore();

        if (choice == 1) {
            std::cout << "Enter expression (e.g., '12.5 * 4' or '100 / 2'): ";
            std::string expr;
            std::getline(std::cin, expr);
            std::string query = "CALC " + expr;
            send(sock_fd, query.c_str(), query.length(), 0);

            memset(buffer, 0, BUFFER_SIZE);
            recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
            std::cout << "[Server] " << buffer;
        } 
        else if (choice == 2) {
            std::cout << "Enter command and string (e.g., 'UPPER hello' | 'REVERSE world' | 'LENGTH test'): ";
            std::string sop;
            std::getline(std::cin, sop);
            std::string query = "STR " + sop;
            send(sock_fd, query.c_str(), query.length(), 0);

            memset(buffer, 0, BUFFER_SIZE);
            recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
            std::cout << "[Server] " << buffer;
        } 
        else if (choice == 3) {
            std::string query = "FILE";
            send(sock_fd, query.c_str(), query.length(), 0);

            memset(buffer, 0, BUFFER_SIZE);
            recv(sock_fd, buffer, BUFFER_SIZE - 1, 0); // Server handshake confirmation
            handle_file_service(sock_fd);
        } 
        else if (choice == 4) {
            std::string query = "TIME";
            send(sock_fd, query.c_str(), query.length(), 0);

            memset(buffer, 0, BUFFER_SIZE);
            recv(sock_fd, buffer, BUFFER_SIZE - 1, 0);
            std::cout << "[Server] " << buffer;
        } 
        else if (choice == 5) {
            send(sock_fd, "EXIT", 4, 0);
            break;
        }
    }

    close(sock_fd);
    return 0;
}
