#include "net/TcpConnect.hpp"
#include "utils/byte_tools.hpp"

TcpConnect::TcpConnect(std::string ip, int port, std::chrono::milliseconds connect_timeout, std::chrono::milliseconds read_timeout) :
    ip(ip),
    port(port),
    connect_timeout(connect_timeout),
    read_timeout(read_timeout)
    {
        sock = socket(AF_INET, SOCK_STREAM, 0);
    }

TcpConnect::~TcpConnect() {
    close(sock);
}

void TcpConnect::EstablishConnection() {
    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(ip.c_str());
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    fd_set fdset;
    struct timeval time_val;

    int current_state = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, current_state | O_NONBLOCK);
    int code = connect(sock, (struct sockaddr*) &server, sizeof(struct sockaddr_in));
    if (code == 0) {
        current_state = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, current_state & ~O_NONBLOCK);
        return;
    }

    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    time_val.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(connect_timeout).count();
    time_val.tv_usec = 0;

    code = select(sock + 1, NULL, &fdset, NULL, &time_val);
    switch (code) {
        case 0:
            close(sock);
            throw std::runtime_error("Connection timeout");
            break;

        case 1:
        default:
            int soError;
            socklen_t ln = sizeof soError;
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &ln);

            if (soError == 0) {
                current_state = fcntl(sock, F_GETFL, 0);
                fcntl(sock, F_SETFL, current_state & ~O_NONBLOCK);
                return;
            }

            close(sock);
            throw std::runtime_error("Socket connection error");
    }
}

void TcpConnect::SendData(const std::string& data) const {
    char to_send[data.size()];
    for (int i = 0; i < data.size(); ++i) {
        to_send[i] = data[i];
    }

    if ((send(sock, to_send, data.size(), 0)) < 0) {
        std::cout << "Send error: " << strerror(errno) << ' ' << errno << '\n';
        close(sock);
        throw std::runtime_error("Send error");
    }
}

std::string TcpConnect::ReceiveData(size_t buffer_size) const {
    std::string message;
    if (!buffer_size) {
        struct pollfd fd;
        fd.fd = sock;
        fd.events = POLLIN;
        int cd = poll(&fd, 1, read_timeout.count());
        char data[4];

        switch (cd) {
            case -1:
                close(sock);
                throw std::runtime_error("Poll error");

            case 0:
                close(sock);
                throw std::runtime_error("Read timeout");

            default:
                recv(sock, data, sizeof(data), 0);
        }

        for (char& ch : data) {
            message += ch;
        }
        buffer_size = utils::BytesToInt(message);
    }

    if (buffer_size > 100'000) {
        close(sock);
        throw std::runtime_error("Too much data");
    }

    int to_read = buffer_size;
    char data_2[buffer_size];
    while (to_read) {
        int read = recv(sock, data_2, sizeof(data_2), 0);
        if (read <= 0) {
            close(sock);
            throw std::runtime_error("Read error");
        }
        for (int i = 0; i < read; ++i) {
            message += data_2[i];
        }
        to_read -= read;
    }

    return message;
}

void TcpConnect::CloseConnection() {
    close(sock);
}

const std::string &TcpConnect::GetIp() const {
    return ip;
}

int TcpConnect::GetPort() const {
    return port;
}
