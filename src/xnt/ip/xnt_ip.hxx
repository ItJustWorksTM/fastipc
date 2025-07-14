#pragma once

#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "io/addr.hxx"
#include "channel.hxx"

namespace fastipc::xnt {

using LocalIndex = std::uint32_t;
using RemoteIndex = std::uint32_t;

class IPTransport {
    int m_port_number;
    int m_data_sockfd;
    std::thread m_control_thread;
    std::thread m_data_thread;

    std::vector<std::pair<std::string, LocalIndex>> pending;
    std::unordered_map<std::string, LocalIndex> provides;
    std::unordered_map<io::SocketAddrV4, std::array<RemoteIndex, 128uz>> index_mapping;
    std::vector<io::SocketAddrV4> m_remotes{}; // Temporary

    void serve_control();
    void run_control(int client_sock);
    void run_data();
    void send_sample(const impl::ChannelSample& sample);

  public:
    explicit IPTransport(int port_number);

    void connect_remote(std::string_view locator);
    void register_channel();
    void unregister_channel();
};

} // namespace fastipc::xnt
