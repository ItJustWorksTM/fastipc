#include <cassert>
#include <cstddef>
#include <thread>

#include "fastipc.hxx"
#include "tower.hxx"

int main() {

    auto tower = fastipc::Tower::create("fastipcd");
    std::jthread tower_thread{[&] { tower.run(); }};

    constexpr std::string_view channel_name{"Hallowed are the Ori"};
    constexpr std::size_t max_payload_size{sizeof(int)};

    fastipc::Writer writer{channel_name, max_payload_size};
    fastipc::Reader reader{channel_name, max_payload_size};

    {
        auto sample = reader.acquire();
        assert(sample.getSequenceId() == 0);
        reader.release(std::move(sample));
    }

    {
        auto sample = writer.prepare();
        assert(sample.getSequenceId() == 1);
        *static_cast<int*>(sample.getPayload()) = 5;
        writer.submit(std::move(sample));
    }

    {
        auto sample = reader.acquire();
        assert(sample.getSequenceId() == 1);
        assert(*static_cast<const int*>(sample.getPayload()) == 5);
        reader.release(std::move(sample));
    }

    tower.shutdown();
}
