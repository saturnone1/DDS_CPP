#pragma once

#include "dds_cpp/dds_client_options.hpp"
#include "dds_cpp/dds_transport.hpp"

#include <memory>

namespace dds_cpp {

class DDSCPP_API RtiDdsTransport final : public DdsTransport {
public:
    explicit RtiDdsTransport(DdsClientOptions options);
    ~RtiDdsTransport() override;

    void create_publisher(const TopicDefinition& topic, std::string sample_type_name) override;
    std::unique_ptr<Subscription> subscribe(
        const TopicDefinition& topic,
        std::string sample_type_name,
        SampleHandler handler) override;
    void publish(const TopicDefinition& topic, std::string sample_type_name, ByteSpan sample) override;
    void create_typed_publisher(const TopicDefinition& topic, const std::type_info& sample_type) override;
    std::unique_ptr<Subscription> subscribe_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler) override;
    void publish_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) override;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace dds_cpp
