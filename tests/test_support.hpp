#include "DDSSim_messages.hpp"
#pragma once
#include "Telemetry_messages.hpp"
#include "dds_cpp/DdsCpp.hpp"
#include "ClientAccess.hpp"
#include "InMemoryDdsTransport.hpp"
#include "DdsConfigurationReader.hpp"

#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>


namespace dds_test {
namespace MSG = DdsModules_DDSSim::MSG;
namespace ENUM = DdsModules_DDSSim::ENUM;
namespace Telemetry = DdsModules_Telemetry::Telemetry;
using namespace std::chrono_literals;
class BrokenOutput final {
    class Buffer final : public std::streambuf {
        std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
        int_type overflow(int_type) override { return traits_type::eof(); }
    } buffer_;
    std::ostream& stream_;
    std::streambuf* original_;
    std::ios_base::iostate exceptions_, state_;
public:
    explicit BrokenOutput(std::ostream& stream) : stream_(stream), original_(stream.rdbuf()),
        exceptions_(stream.exceptions()), state_(stream.rdstate()) {
        stream_.exceptions(std::ios_base::goodbit);
        stream_.rdbuf(&buffer_);
        stream_.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    }
    ~BrokenOutput() noexcept {
        try {
            stream_.exceptions(std::ios_base::goodbit);
            stream_.rdbuf(original_);
            stream_.clear(state_);
            stream_.exceptions(exceptions_);
        } catch (...) { }
    }
};

inline void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

inline void set_environment(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) unsetenv(name);
    else setenv(name, value, 1);
#endif
}

inline std::filesystem::path find_repo_root()
{
    auto current = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::is_directory(current / "definitions") &&
            std::filesystem::is_directory(current / "DDSCPP")) return current;
        if (!current.has_parent_path() || current.parent_path() == current) break;
        current = current.parent_path();
    }
    throw std::runtime_error("Could not find DDSClient repository root.");
}

inline dds_cpp::DdsClientOptions in_memory_options(const std::filesystem::path& definitions)
{
    dds_cpp::DdsClientOptions options;
    options.MessageModules() = {dds_cpp::DDSSimMessages()};
    options.QosProfilesXmlPath() = (definitions / "qos_profiles.xml").string();
    return options;
}

inline dds_cpp::DdsClient test_client(dds_cpp::DdsClientOptions options)
{
    return dds_cpp::detail::ClientAccess::Create(std::move(options), std::make_unique<dds_cpp::InMemoryDdsTransport>());
}

void run_rti_tests(const std::filesystem::path& definitions);
} // namespace dds_test
