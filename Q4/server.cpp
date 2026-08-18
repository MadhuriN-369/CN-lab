#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "packet.h"

// Set Loss Probability Percentage (e.g., 25% chance of packet loss)
#define SIMULATED_LOSS_PERCENT 25 

int main() {
    srand(static_cast<unsigned int>(time(nullptr)));

    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    std::cout << "[*] Reliable UDP Server listening on port " << SERVER_PORT << "..." << std::endl;
    std::cout << "[*] Simulated Packet Loss Rate: " << SIMULATED_LOSS_PERCENT << "%" << std::endl;

    std::ofstream outfile("received_file.bin", std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "[!] Failed to create output file." << std::endl;
        close(server_fd);
        return 1;
    }

    uint32_t expected_seq = 0;
    Packet pkt{};
    AckPacket ack{};
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    while (true) {
        memset(&pkt, 0, sizeof(Packet));
        ssize_t bytes_recv = recvfrom(server_fd, &pkt, sizeof(Packet), 0,
                                      (struct sockaddr*)&client_addr, &addr_len);

        if (bytes_recv <= 0) continue;

        // Packet Loss Simulation Check
        if ((rand() % 100) < SIMULATED_LOSS_PERCENT) {
            std::cout << " [!] [SIMULATED LOSS] Dropped incoming packet Seq: " << pkt.seq_num << std::endl;
            continue; // Drop the packet silently without sending an ACK
        }

        // Process Expected In-Order Packet
        if (pkt.seq_num == expected_seq) {
            std::cout << " [+] Received Packet Seq: " << pkt.seq_num 
                      << " (" << pkt.data_length << " bytes)" << std::endl;
            
            outfile.write(pkt.data, pkt.data_length);
            
            // Send ACK for this packet
            ack.ack_num = pkt.seq_num;
            sendto(server_fd, &ack, sizeof(AckPacket), 0,
                   (struct sockaddr*)&client_addr, addr_len);
            std::cout << "  -> Sent ACK: " << ack.ack_num << std::endl;

            expected_seq++;

            if (pkt.is_last) {
                std::cout << "\n[✓] End of File received. Transfer complete!\n";
                // Send redundant ACKs to make sure the client knows it finished
                for (int i = 0; i < 3; ++i) {
                    sendto(server_fd, &ack, sizeof(AckPacket), 0, (struct sockaddr*)&client_addr, addr_len);
                }
                break;
            }
        } 
        // Duplicate Packet Received (Sender retransmitted because an earlier ACK was lost/delayed)
        else if (pkt.seq_num < expected_seq) {
            std::cout << " [?] Duplicate Packet Seq: " << pkt.seq_num 
                      << " (Resending ACK: " << pkt.seq_num << ")" << std::endl;
            ack.ack_num = pkt.seq_num;
            sendto(server_fd, &ack, sizeof(AckPacket), 0,
                   (struct sockaddr*)&client_addr, addr_len);
        }
    }

    outfile.close();
    close(server_fd);
    return 0;
}
