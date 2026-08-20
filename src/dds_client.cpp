#include "dds_cpp/dds_client.hpp"

#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>
#if defined(__GNUG__)
#include <cxxabi.h>
#include <cstdlib>
#endif

#include "dds_cpp/dds_configuration_loader.hpp"
#include "dds_cpp/dds_exception.hpp"
#include "dds_cpp/in_memory_dds_transport.hpp"
#include "dds_cpp/rti_dds_transport.hpp"

namespace dds_cpp {

DdsException::DdsException(const std::string& message)
    : std::runtime_error(message)
{
}

DdsConfigurationException::DdsConfigurationException(const std::string& message)
    : DdsException(message)
{
}

DdsOperationException::DdsOperationException(const std::string& message)
    : DdsException(message)
{
}

DdsClient DdsClient::connect()
{
    return DdsClient(DdsClientOptions::from_environment());
}

DdsClient DdsClient::connect(const std::string& config_path)
{
    return DdsClient(DdsClientOptions::load(config_path));
}

DdsClient DdsClient::connect(DdsClientOptions options)
{
    return DdsClient(std::move(options));
}

DdsClient::DdsClient()
    : DdsClient(DdsClientOptions::load())
{
}

DdsClient::DdsClient(DdsClientOptions options)
    : DdsClient(options, DdsConfigurationLoader::load(options))
{
}

DdsClient::DdsClient(DdsClientOptions options, std::unique_ptr<DdsTransport> transport)
    : DdsClient(options, DdsConfigurationLoader::load(options), std::move(transport))
{
}

DdsClient::DdsClient(DdsClientOptions options, DdsConfiguration configuration)
    : DdsClient(options, std::move(configuration), create_default_transport(options))
{
}

DdsClient::DdsClient(DdsClientOptions options, DdsConfiguration configuration, std::unique_ptr<DdsTransport> transport)
    : options_(std::move(options))
    , configuration_(std::move(configuration))
    , transport_(std::move(transport))
{
    if (!transport_) {
        throw DdsOperationException("DdsClient requires a transport.");
    }
}

DdsClient::DdsClient(DdsClient&& other) noexcept
    : options_(std::move(other.options_))
    , configuration_(std::move(other.configuration_))
{
    std::lock_guard lock(other.transport_mutex_);
    transport_ = std::move(other.transport_);
}

DdsClient& DdsClient::operator=(DdsClient&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    std::scoped_lock lock(transport_mutex_, other.transport_mutex_);
    options_ = std::move(other.options_);
    configuration_ = std::move(other.configuration_);
    transport_ = std::move(other.transport_);
    return *this;
}

const TopicDefinition& DdsClient::resolve_topic(const std::type_info& sample_type) const
{
    std::string full_name;
#if defined(__GNUG__)
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> demangled(
        abi::__cxa_demangle(sample_type.name(), nullptr, nullptr, &status), std::free);
    full_name = status == 0 && demangled ? demangled.get() : sample_type.name();
#else
    full_name = sample_type.name();
#endif
    const auto namespace_separator = full_name.rfind("::");
    auto type_name = namespace_separator == std::string::npos
        ? full_name
        : full_name.substr(namespace_separator + 2);
    for (const auto* prefix : {"class ", "struct "}) {
        if (type_name.starts_with(prefix)) type_name.erase(0, std::char_traits<char>::length(prefix));
    }
    if (const auto* topic = configuration_.try_get_topic(type_name)) return *topic;
    throw DdsConfigurationException(
        "No topic is defined for sample type '" + full_name +
        "'. Expected a topics.xml entry named '" + type_name + "'.");
}

DdsClient::~DdsClient()
{
    close();
}

void DdsClient::close() noexcept
{
    std::lock_guard lock(transport_mutex_);
    transport_.reset();
}

bool DdsClient::is_open() const noexcept
{
    std::lock_guard lock(transport_mutex_);
    return transport_ != nullptr;
}

std::shared_ptr<DdsTransport> DdsClient::require_transport() const
{
    std::lock_guard lock(transport_mutex_);
    if (!transport_) {
        throw DdsOperationException("The DDS client is closed.");
    }
    return transport_;
}

const DdsClientOptions& DdsClient::options() const noexcept
{
    return options_;
}

const DdsConfiguration& DdsClient::configuration() const noexcept
{
    return configuration_;
}

void DdsClient::create_publisher(const std::string& topic_name, std::string sample_type_name)
{
    const auto transport = require_transport();
    const auto& topic = configuration_.get_topic(topic_name);
    ensure_can_publish(topic);
    transport->create_publisher(topic, std::move(sample_type_name));
}

std::unique_ptr<Subscription> DdsClient::subscribe(
    const std::string& topic_name,
    std::string sample_type_name,
    SampleHandler handler)
{
    const auto transport = require_transport();
    const auto& topic = configuration_.get_topic(topic_name);
    ensure_can_subscribe(topic);
    return transport->subscribe(topic, std::move(sample_type_name), std::move(handler));
}

void DdsClient::publish(const std::string& topic_name, std::string sample_type_name, ByteSpan sample)
{
    const auto transport = require_transport();
    const auto& topic = configuration_.get_topic(topic_name);
    ensure_can_publish(topic);

    if (options_.log_level == DdsLogLevel::Debug) {
        std::clog << "TX topic=" << topic.name << ", type=" << sample_type_name << ", bytes=" << sample.size() << '\n';
    }

    transport->publish(topic, std::move(sample_type_name), sample);
}

std::unique_ptr<DdsTransport> DdsClient::create_default_transport(const DdsClientOptions& options)
{
    if (options.use_rti_transport) {
        return std::make_unique<RtiDdsTransport>(options);
    }

    return std::make_unique<InMemoryDdsTransport>();
}

void DdsClient::ensure_can_publish(const TopicDefinition& topic)
{
    if (topic.direction == TopicDirection::Subscribe) {
        throw DdsOperationException("Topic '" + topic.name + "' is configured as Subscribe and cannot publish.");
    }
}

void DdsClient::ensure_can_subscribe(const TopicDefinition& topic)
{
    if (topic.direction == TopicDirection::Publish) {
        throw DdsOperationException("Topic '" + topic.name + "' is configured as Publish and cannot subscribe.");
    }
}

} // namespace dds_cpp
