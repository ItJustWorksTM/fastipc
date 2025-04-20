#include <print>
#include <string_view>

#include <fastipc.hxx>

using namespace std::literals;

int main() {
    fastipc::Reader reader{"channel"sv, 256u};

    auto sample = reader.acquire();

    std::println("value for seq-id {}: {}", sample.getSequenceId(), static_cast<const char*>(sample.getPayload()));
}
