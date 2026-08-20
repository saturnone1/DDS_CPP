#include <dds_cpp/dds_cpp.hpp>

#include <iostream>

int main()
{
    const auto options = dds_cpp::DdsClientOptions::Load();
    std::cout << options.DomainId() << '\n';
    return 0;
}
