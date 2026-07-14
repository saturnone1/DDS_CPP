#pragma once

#include <stdexcept>
#include <string>

#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

class DDSCPP_API DdsException : public std::runtime_error {
public:
    explicit DdsException(const std::string& message);
};

class DDSCPP_API DdsConfigurationException final : public DdsException {
public:
    explicit DdsConfigurationException(const std::string& message);
};

class DDSCPP_API DdsOperationException final : public DdsException {
public:
    explicit DdsOperationException(const std::string& message);
};

} // namespace dds_cpp
