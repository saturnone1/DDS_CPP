#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <typeinfo>

#include "dds_cpp/topic_definition.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

using ByteSpan = std::span<const std::byte>;
using SampleHandler = std::function<void(ByteSpan)>;
using TypedSampleHandler = std::function<void(const void*)>;

class DDSCPP_API Subscription {
public:
    virtual ~Subscription() = default;
};

class DDSCPP_API DdsTransport {
public:
    virtual ~DdsTransport() = default;

    virtual void create_publisher(const TopicDefinition& topic, std::string sample_type_name) = 0;
    virtual std::unique_ptr<Subscription> subscribe(
        const TopicDefinition& topic,
        std::string sample_type_name,
        SampleHandler handler) = 0;
    virtual void publish(
        const TopicDefinition& topic,
        std::string sample_type_name,
        ByteSpan sample) = 0;

    virtual void create_typed_publisher(const TopicDefinition& topic, const std::type_info& sample_type) = 0;
    virtual std::unique_ptr<Subscription> subscribe_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler) = 0;
    virtual void publish_typed(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) = 0;
};

} // namespace dds_cpp
