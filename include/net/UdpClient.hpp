#pragma once

#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <cstring>

class UdpClient {
public:
    UdpClient(const std::string& host, int port, int timeout_sec = 10);
    ~UdpClient();

    std::string SendReceive(const std::string& data);
    uint64_t GenerateTransactionId();

private:
    int sockfd;
    struct sockaddr_in server_address;
    std::string host;
    int port;
    int timeout_sec;
};
