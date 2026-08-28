#include <iostream>
#include <string_view>

int main() {
    constexpr std::string_view message{"low_latency C++20 project is ready"};
    std::cout << message << '\n';
    return 0;
}
