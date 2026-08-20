#pragma once

#include <memory>
#include <mutex>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>

#include "dds_cpp/dds_client_options.hpp"
#include "dds_cpp/dds_configuration.hpp"
#include "dds_cpp/dds_exception.hpp"
#include "dds_cpp/dds_transport.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

namespace detail {

template <class T>
void log_sample(const char* direction, const DdsClientOptions& options, const TopicDefinition& topic, const T& sample)
{
    if (options.log_level != DdsLogLevel::Debug) return;
    std::clog << direction << " topic=" << topic.name << ", type=" << typeid(T).name();
    if constexpr (requires(std::ostream& stream, const T& value) { stream << value; }) {
        std::clog << '\n' << sample;
    }
    std::clog << '\n';
}

} // namespace detail

template <class T>
class DdsPublisher final {
public:
    [[nodiscard]] const TopicDefinition& topic() const noexcept { return topic_; }
    [[nodiscard]] const TopicDefinition& Topic() const noexcept { return topic(); }

    void publish(const T& sample) const
    {
        detail::log_sample("TX", options_, topic_, sample);
        const auto transport = transport_.lock();
        if (!transport) {
            throw DdsOperationException("The DDS client that created this publisher is closed.");
        }
        transport->publish_typed(topic_, typeid(T), &sample);
    }

    void Publish(const T& sample) const { publish(sample); }

private:
    friend class DdsClient;
    DdsPublisher(TopicDefinition topic, DdsClientOptions options, std::shared_ptr<DdsTransport> transport)
        : topic_(std::move(topic)), options_(std::move(options)), transport_(std::move(transport)) {}

    TopicDefinition topic_;
    DdsClientOptions options_;
    std::weak_ptr<DdsTransport> transport_;
};

class DDSCPP_API DdsClient final {
public:
    static DdsClient connect();
    static DdsClient connect(const std::string& config_path);
    static DdsClient connect(DdsClientOptions options);

    static DdsClient Connect() { return connect(); }
    static DdsClient Connect(const std::string& config_path) { return connect(config_path); }
    static DdsClient Connect(DdsClientOptions options) { return connect(std::move(options)); }

    DdsClient();
    explicit DdsClient(DdsClientOptions options);
    DdsClient(DdsClientOptions options, std::unique_ptr<DdsTransport> transport);
    DdsClient(DdsClientOptions options, DdsConfiguration configuration);
    DdsClient(DdsClientOptions options, DdsConfiguration configuration, std::unique_ptr<DdsTransport> transport);

    DdsClient(const DdsClient&) = delete;
    DdsClient& operator=(const DdsClient&) = delete;
    DdsClient(DdsClient&& other) noexcept;
    DdsClient& operator=(DdsClient&& other) noexcept;
    ~DdsClient();

    void close() noexcept;
    void Close() noexcept { close(); }
    void Dispose() noexcept { close(); }
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] const DdsClientOptions& options() const noexcept;
    [[nodiscard]] const DdsConfiguration& configuration() const noexcept;
    [[nodiscard]] const DdsClientOptions& Options() const noexcept { return options(); }
    [[nodiscard]] const DdsConfiguration& Configuration() const noexcept { return configuration(); }

    void create_publisher(const std::string& topic_name, std::string sample_type_name);
    std::unique_ptr<Subscription> subscribe(
        const std::string& topic_name,
        std::string sample_type_name,
        SampleHandler handler);
    void publish(const std::string& topic_name, std::string sample_type_name, ByteSpan sample);

    template <class T>
    void create_publisher(const std::string& topic_name)
    {
        const auto transport = require_transport();
        const auto& topic = configuration_.get_topic(topic_name);
        ensure_can_publish(topic);
        transport->create_typed_publisher(topic, typeid(T));
    }

    template <class T>
    [[nodiscard]] DdsPublisher<T> create_publisher()
    {
        const auto transport = require_transport();
        const auto& topic = resolve_topic(typeid(T));
        ensure_can_publish(topic);
        transport->create_typed_publisher(topic, typeid(T));
        return DdsPublisher<T>(topic, options_, transport);
    }

    template <class T>
    std::unique_ptr<Subscription> subscribe(const std::string& topic_name, std::function<void(const T&)> handler)
    {
        const auto transport = require_transport();
        if (!handler) {
            throw std::invalid_argument("DDSCPP typed subscribe requires a handler.");
        }
        const auto& topic = configuration_.get_topic(topic_name);
        ensure_can_subscribe(topic);
        return transport->subscribe_typed(topic, typeid(T), [options = options_, topic, handler = std::move(handler)](const void* sample) {
            const auto& typed_sample = *static_cast<const T*>(sample);
            detail::log_sample("RX", options, topic, typed_sample);
            handler(typed_sample);
        });
    }

    template <class T>
    std::unique_ptr<Subscription> subscribe(std::function<void(const T&)> handler)
    {
        return subscribe<T>(resolve_topic(typeid(T)).name, std::move(handler));
    }

    template <class T>
    void publish(const std::string& topic_name, const T& sample)
    {
        const auto transport = require_transport();
        const auto& topic = configuration_.get_topic(topic_name);
        ensure_can_publish(topic);
        detail::log_sample("TX", options_, topic, sample);
        transport->publish_typed(topic, typeid(T), &sample);
    }

    template <class T>
    void publish(const T& sample)
    {
        publish(resolve_topic(typeid(T)).name, sample);
    }

    template <class T> [[nodiscard]] DdsPublisher<T> CreatePublisher() { return create_publisher<T>(); }
    template <class T> std::unique_ptr<Subscription> CreateSubscriber(std::function<void(const T&)> handler)
    {
        return subscribe<T>(std::move(handler));
    }
    template <class T> std::unique_ptr<Subscription> Subscribe(std::function<void(const T&)> handler)
    {
        return subscribe<T>(std::move(handler));
    }
    template <class T> std::unique_ptr<Subscription> Subscribe(
        const std::string& topic_name, std::function<void(const T&)> handler)
    {
        return subscribe<T>(topic_name, std::move(handler));
    }
    template <class T> void Publish(const T& sample) { publish(sample); }
    template <class T> void Publish(const std::string& topic_name, const T& sample) { publish(topic_name, sample); }

private:
    static std::unique_ptr<DdsTransport> create_default_transport(const DdsClientOptions& options);
    static void ensure_can_publish(const TopicDefinition& topic);
    static void ensure_can_subscribe(const TopicDefinition& topic);
    [[nodiscard]] std::shared_ptr<DdsTransport> require_transport() const;
    [[nodiscard]] const TopicDefinition& resolve_topic(const std::type_info& sample_type) const;

    DdsClientOptions options_;
    DdsConfiguration configuration_;
    mutable std::mutex transport_mutex_;
    std::shared_ptr<DdsTransport> transport_;
};

} // namespace dds_cpp
