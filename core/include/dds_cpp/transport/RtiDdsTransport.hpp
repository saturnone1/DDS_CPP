#pragma once

#include "dds_cpp/configuration/DdsClientOptions.hpp"
#include "IDdsTransport.hpp"

#include <memory>

namespace dds_cpp {

class DDSCPP_API RtiDdsTransport final : public IDdsTransport {
public:
    explicit RtiDdsTransport(DdsClientOptions options);
    ~RtiDdsTransport() override;
    void Dispose() noexcept override;

    void WaitForSubscribers(const TopicDefinition&, const std::type_info&, int, std::chrono::milliseconds) override;
    void WaitForAcknowledgments(const TopicDefinition&, const std::type_info&, std::chrono::milliseconds) override;
    void CreatePublisher(const TopicDefinition& topic, const std::type_info& sample_type) override;
    std::function<void(const void*)> BindPublisher(const TopicDefinition& topic, const std::type_info& sample_type) override;
    std::unique_ptr<Subscription> Subscribe(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler, std::uint64_t subscription_id) override;
    void Publish(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) override;

private:
    class TransportState;
    std::shared_ptr<TransportState> state_;
};

} // namespace dds_cpp
