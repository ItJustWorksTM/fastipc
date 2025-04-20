#include "tower.hxx"

int main() {
    auto tower = fastipc::Tower::create("fastipcd");
    tower.run();
}
