#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#define PORT 65434
#define BUFFER_SIZE 4096

// Helper to calculate SHA-256 of a saved binary file
std::string calculate_file_sha256(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";

    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    char buffer[BUFFER_SIZE];

    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Reliable read helper for fixed header/byte counts
bool read_exact(int sock, char* buffer, size_t size) {
    size_t total_received = 0;
    while (total_received < size) {
        ssize_t bytes = recv(sock, buffer + total_received, size - total_received, 0);
        if (bytes <= 0) return false;
        total_received += bytes;
    }
    return true;
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

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] File Transfer Server listening on port " << PORT << "..." << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) continue;

        std::cout << "\n[+] Incoming connection accepted." << std::endl;

        // Protocol Step 1: Read filename length (4 bytes uint32_t) & filename
        uint32_t name_len_net = 0;
        if (!read_exact(client_fd, (char*)&name_len_net, sizeof(name_len_net))) {
            close(client_fd);
            continue;
        }
        uint32_t name_len = ntohl(name_len_net);

        std::vector<char> name_buf(name_len + 1, 0);
        if (!read_exact(client_fd, name_buf.data(), name_len)) {
            close(client_fd);
            continue;
        }
        std::string original_filename(name_buf.data());
        std::string saved_filename = "received_" + original_filename;

        // Protocol Step 2: Read file size (8 bytes uint64_t)
        uint64_t file_size_net = 0;
        if (!read_exact(client_fd, (char*)&file_size_net, sizeof(file_size_net))) {
            close(client_fd);
            continue;
        }
        uint64_t file_size = be64toh(file_size_net);

        // Protocol Step 3: Read SHA-256 checksum string (64 characters)
        char client_checksum[65] = {0};
        if (!read_exact(client_fd, client_checksum, 64)) {
            close(client_fd);
            continue;
        }

        std::cout << "[*] Receiving File: " << original_filename << " (" << file_size << " bytes)" << std::endl;
        std::cout << "[*] Expected SHA-256: " << client_checksum << std::endl;

        // Protocol Step 4: Stream binary payload directly into disk
        std::ofstream outfile(saved_filename, std::ios::binary);
        if (!outfile.is_open()) {
            const char* err_resp = "STATUS:ERROR_CANNOT_WRITE";
            send(client_fd, err_resp, strlen(err_resp), 0);
            close(client_fd);
            continue;
        }

        char buffer[BUFFER_SIZE];
        uint64_t total_received = 0;
        bool transfer_ok = true;

        while (total_received < file_size) {
            size_t bytes_to_read = std::min((uint64_t)BUFFER_SIZE, file_size - total_received);
            ssize_t bytes = recv(client_fd, buffer, bytes_to_read, 0);
            if (bytes <= 0) {
                transfer_ok = false;
                break;
            }
            outfile.write(buffer, bytes);
            total_received += bytes;
        }
        outfile.close();

        if (!transfer_ok) {
            std::cout << "[!] Connection dropped before file completed." << std::endl;
            close(client_fd);
            continue;
        }

        // Protocol Step 5: Verify Checksum Integrity
        std::string server_checksum = calculate_file_sha256(saved_filename);
        std::cout << "[*] Calculated SHA-256: " << server_checksum << std::endl;

        std::string response;
        if (server_checksum == std::string(client_checksum)) {
            std::cout << "[✓] Integrity Check Passed! File saved as: " << saved_filename << std::endl;
            response = "STATUS:SUCCESS - Checksums Match. File verified.";
        } else {
            std::cout << "[✗] Integrity Check Failed! File corrupted." << std::endl;
            response = "STATUS:CORRUPTED - Checksum mismatch.";
        }

        send(client_fd, response.c_str(), response.length(), 0);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
