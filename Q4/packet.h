#pragma once
#include <cstdint>

#define PACKET_PAYLOAD_SIZE 1024
#define SERVER_PORT 65435
#define CLIENT_TIMEOUT_SEC 1

// Custom Packet Structure
struct Packet {
    uint32_t seq_num;                      // Packet Sequence Number
    uint32_t data_length;                  // Actual payload size (0 <= size <= 1024)
    bool is_last;                          // EOF Flag
    char data[PACKET_PAYLOAD_SIZE];        // File Chunk
};

// Custom ACK Structure
struct AckPacket {
    uint32_t ack_num;                      // Acknowledged Sequence Number
};
