#include <cstdio>
#include <print>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include "xnt_ip.hxx"

namespace fastipc::xnt {

void IPTransport::serve_control() {

    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::perror("socket failed");
        return;
    }

    // Allow address reuse
    constexpr int opt = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind socket to IP/port
    ::sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Listen on all interfaces
    server_addr.sin_port = ::htons(m_port_number);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::perror("bind failed");
        ::close(server_fd);
        return;
    }

    // Listen
    if (::listen(server_fd, 128) < 0) {
        std::perror("listen failed");
        ::close(server_fd);
        return;
    }

    std::println("Server listening on port {}", m_port_number);

    // Accept loop
    for (;;) {
        ::sockaddr_in client_addr{};
        ::socklen_t client_len{};
        const int client_sock = ::accept(server_fd, reinterpret_cast<::sockaddr*>(&client_addr), &client_len);
        if (client_sock < 0) {
            std::perror("accept failed");
            continue;
        }

        std::println("New connection from {}", ::inet_ntoa(client_addr.sin_addr));
        std::thread{&IPTransport::run_control, this, client_sock}.detach();
    }

    ::close(server_fd);
}

void IPTransport::run_control(int client_sock) {}

} // namespace fastipc::xnt
