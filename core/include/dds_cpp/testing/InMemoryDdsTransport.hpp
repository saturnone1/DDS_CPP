#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "IDdsTransport.hpp"

namespace dds_cpp {

class DDSCPP_API InMemoryDdsTransport final : public IDdsTransport {
public:
    InMemoryDdsTransport();
    ~InMemoryDdsTransport() override;
    void Dispose() noexcept override;
    void WaitForSubscribers(const TopicDefinition&, const std::type_info&, int, std::chrono::milliseconds) override;
    void WaitForAcknowledgments(const TopicDefinition&, const std::type_info&, std::chrono::milliseconds) override;

    void CreatePublisher(const TopicDefinition& topic, const std::type_info& sample_type) override;
    std::unique_ptr<Subscription> Subscribe(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        TypedSampleHandler handler, std::uint64_t subscription_id) override;
    void Publish(
        const TopicDefinition& topic,
        const std::type_info& sample_type,
        const void* sample) override;

private:
    struct State;
    class InMemorySubscription;
    std::shared_ptr<State> state_;
};

} // namespace dds_cpp
