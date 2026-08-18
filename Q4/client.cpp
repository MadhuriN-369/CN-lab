#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "packet.h"

#define SERVER_IP "127.0.0.1"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file_to_send>" << std::endl;
        return 1;
    }

    std::string filepath = argv[1];
    std::ifstream infile(filepath, std::ios::binary | std::ios::ate);
    if (!infile.is_open()) {
        std::cerr << "[!] Cannot open file: " << filepath << std::endl;
        return 1;
    }

    std::streamsize file_size = infile.tellg();
    infile.seekg(0, std::ios::beg);

    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Set Socket Receive Timeout for ACK monitoring
    struct timeval tv;
    tv.tv_sec = CLIENT_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    std::cout << "[*] Sending '" << filepath << "' (" << file_size << " bytes) to " 
              << SERVER_IP << ":" << SERVER_PORT << std::endl;

    uint32_t current_seq = 0;
    std::streamsize bytes_sent_total = 0;
    Packet pkt{};
    AckPacket ack{};
    socklen_t addr_len = sizeof(server_addr);

    while (bytes_sent_total < file_size || (file_size == 0 && current_seq == 0)) {
        memset(&pkt, 0, sizeof(Packet));
        pkt.seq_num = current_seq;

        infile.read(pkt.data, PACKET_PAYLOAD_SIZE);
        pkt.data_length = static_cast<uint32_t>(infile.gcount());
        bytes_sent_total += pkt.data_length;
        pkt.is_last = (bytes_sent_total >= file_size);

        // Stop-and-Wait Transmission with Retransmission Loop
        while (true) {
            std::cout << "[*] Sending Packet Seq: " << pkt.seq_num << " (" << pkt.data_length << " bytes)..." << std::endl;
            sendto(sock_fd, &pkt, sizeof(Packet), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

            // Wait for ACK
            memset(&ack, 0, sizeof(AckPacket));
            ssize_t bytes_recv = recvfrom(sock_fd, &ack, sizeof(AckPacket), 0,
                                          (struct sockaddr*)&server_addr, &addr_len);

            if (bytes_recv < 0) {
                // Timeout fired - packet or ACK was dropped
                std::cout << " [!] Timeout occurred! No ACK received for Seq: " << current_seq 
                          << ". Retransmitting..." << std::endl;
                continue;
            }

            if (ack.ack_num == current_seq) {
                std::cout << " [✓] Received ACK for Seq: " << ack.ack_num << std::endl;
                break; // Proceed to next packet
            }
        }

        current_seq++;
        if (pkt.is_last) break;
    }

    infile.close();
    close(sock_fd);
    std::cout << "\n[*] File transfer completed successfully with all lost packets recovered." << std::endl;
    return 0;
}
