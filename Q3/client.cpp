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

#define SERVER_IP "127.0.0.1"
#define PORT 65434
#define BUFFER_SIZE 4096

// Calculate SHA-256 of file before sending
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

std::string get_base_filename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <path_to_binary_file>" << std::endl;
        return 1;
    }

    std::string file_path = argv[1];
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[!] Error: Cannot open file: " << file_path << std::endl;
        return 1;
    }

    uint64_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "[*] Computing SHA-256 for: " << file_path << "..." << std::endl;
    std::string checksum = calculate_file_sha256(file_path);
    std::string filename = get_base_filename(file_path);

    std::cout << "[*] File size: " << file_size << " bytes" << std::endl;
    std::cout << "[*] SHA-256: " << checksum << std::endl;

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
        perror("Connection to server failed");
        close(sock_fd);
        return 1;
    }

    // Send Metadata: [Name Length (4B)] + [Filename] + [File Size (8B)] + [SHA-256 (64B)]
    uint32_t name_len_net = htonl(filename.length());
    send(sock_fd, (char*)&name_len_net, sizeof(name_len_net), 0);
    send(sock_fd, filename.c_str(), filename.length(), 0);

    uint64_t file_size_net = htobe64(file_size);
    send(sock_fd, (char*)&file_size_net, sizeof(file_size_net), 0);
    send(sock_fd, checksum.c_str(), 64, 0);

    // Send Binary File Payload
    std::cout << "[*] Uploading binary payload..." << std::endl;
    char buffer[BUFFER_SIZE];
    while (file.read(buffer, BUFFER_SIZE) || file.gcount() > 0) {
        send(sock_fd, buffer, file.gcount(), 0);
    }
    file.close();

    // Receive Server Verification Response
    char resp_buf[BUFFER_SIZE] = {0};
    ssize_t bytes = recv(sock_fd, resp_buf, BUFFER_SIZE - 1, 0);
    if (bytes > 0) {
        std::cout << "\n[Server Response] " << resp_buf << std::endl;
    }

    close(sock_fd);
    return 0;
}
