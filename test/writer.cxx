#include <iostream>
#include <print>
#include <string_view>

#include <fastipc.hxx>

using namespace std::literals;

int main() {
    fastipc::Writer writer{"channel"sv, 256u};

    auto sample = writer.prepare();

    std::print("Enter value for seq-id {}: ", sample.getSequenceId());
    std::cin.getline(static_cast<char*>(sample.getPayload()), 256u);

    writer.submit(std::move(sample));
}
