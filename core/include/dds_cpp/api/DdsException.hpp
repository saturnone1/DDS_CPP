#pragma once

#include <stdexcept>
#include <string>

#include "dds_cpp/api/Visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsException : public std::runtime_error {
public:
    explicit DdsException(const std::string& message) : std::runtime_error(message) {}
};

} // namespace dds_cpp

