#pragma once

#include <chrono>
#include <stdexcept>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeinfo>

#include "dds_cpp/api/TopicDefinition.hpp"
#include "dds_cpp/api/Visibility.hpp"
#include "dds_cpp/api/Subscription.hpp"
#include "dds_cpp/transport/ClientStatus.hpp"

namespace dds_cpp {

using TypedSampleHandler = std::function<void(const void*)>;


class DDSCPP_API IDdsTransport {
public:
    const std::shared_ptr<detail::ClientStatus>& status() const noexcept { return status_; }
    virtual ~IDdsTransport() = default;
    virtual void Dispose() noexcept = 0;
    virtual void WaitForSubscribers(const TopicDefinition&, const std::type_info&, int, std::chrono::milliseconds) = 0;
    virtual void WaitForAcknowledgments(const TopicDefinition&, const std::type_info&, std::chrono::milliseconds) = 0;

    virtual void CreatePublisher(const TopicDefinition& topic, const std::type_info& sample_type) = 0;
    virtual std::function<void(const void*)> BindPublisher(const TopicDefinition& topic, const std::type_info& sample_type)
    {
        CreatePublisher(topic, sample_type);
        return [this, topic, type = &sample_type](const void* sample) { Publish(topic, *type, sample); };
    }
    virtual std::unique_ptr<Subscription> Subscribe(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler, std::uint64_t subscription_id) = 0;
    virtual void Publish(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) = 0;
protected:
    std::shared_ptr<detail::ClientStatus> status_ = std::make_shared<detail::ClientStatus>();
};

} // namespace dds_cpp
