#include "DDSSim_messages.hpp"
#include "Telemetry_messages.hpp"
#include <dds_cpp/DdsCpp.hpp>
namespace MSG = DdsModules_DDSSim::MSG;
namespace Telemetry = DdsModules_Telemetry::Telemetry;
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#ifdef DDSCLIENT_TEST_AUTO_MODULE
#include "Diagnostics_messages.hpp"
namespace Diagnostics = DdsModules_Diagnostics::Diagnostics;
#endif

int main()
{
#ifdef DDSCLIENT_TEST_NETWORK_ISOLATION
    extern void test_network_isolation();
    test_network_isolation();
#endif
#ifdef DDSCLIENT_TEST_AUTO_MODULE
    {
        dds_cpp::DdsClientOptions auto_options;
        auto_options.DomainId() = 92;
        auto_options.QosProfilesXmlPath() = std::string(DDSCLIENT_TEST_DEFINITIONS) + "/qos_profiles.xml";
        auto_options.Topics() = std::vector<dds_cpp::TopicDefinition>{dds_cpp::Topic::Both<Diagnostics::HealthStatus>()};
        bool rejected = false;
        try { auto invalid = dds_cpp::DdsClient::Connect(auto_options); }
        catch (const dds_cpp::DdsConfigurationException&) { rejected = true; }
        if (!rejected) return 2;
        auto_options.MessageModules() = {dds_cpp::DiagnosticsMessages()};
        std::mutex gate;
        std::condition_variable arrived;
        std::vector<std::uint64_t> values;
        auto receiver = dds_cpp::DdsClient::Connect(auto_options);
        auto subscription = receiver.Subscribe<Diagnostics::HealthStatus>([&](const auto& sample) {
            std::lock_guard lock(gate);
            if (sample.Healthy()) values.push_back(sample.Counter());
            arrived.notify_all();
        });
        auto sender = dds_cpp::DdsClient::Connect(auto_options);
        auto publisher = sender.CreatePublisher<Diagnostics::HealthStatus>();
        publisher.WaitForSubscribers(1, std::chrono::seconds(15));
        for (std::uint64_t i = 1; i <= 4; ++i) {
            Diagnostics::HealthStatus sample; sample.Counter(i); sample.Healthy(true); publisher.Publish(sample);
        }
        publisher.WaitForAcknowledgments(std::chrono::seconds(10));
        std::unique_lock lock(gate);
        if (!arrived.wait_for(lock, std::chrono::seconds(10), [&] { return values.size() >= 4; }) ||
            values != std::vector<std::uint64_t>{1, 2, 3, 4}) return 3;
        std::cout << "Installed auto module: explicit selection + 4/4 RTI samples passed.\n";
    }
#endif
    dds_cpp::DdsClientOptions options;
    options.MessageModules() = {dds_cpp::DDSSimMessages(), dds_cpp::TelemetryMessages()};
    options.DomainId() = 174;
    options.QosProfilesXmlPath() = std::string(DDSCLIENT_TEST_DEFINITIONS) + "/qos_profiles.xml";
    options.Topics() = std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Both<MSG::TimeTickInformation>()};
    auto client = dds_cpp::DdsClient::Connect(options);
    std::mutex mutex;
    std::condition_variable changed;
    bool received = false;
    auto subscription = client.Subscribe<MSG::TimeTickInformation>(
        [&](const MSG::TimeTickInformation& sample) {
            std::lock_guard lock(mutex);
            received = sample.TimeTickMessage().TimeTick() == 42;
            changed.notify_all();
        });
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    MSG::TimeTickInformation sample;
    sample.TimeTickMessage().TimeTick(42);
    for (int attempt = 0; attempt < 30; ++attempt) {
        publisher.Publish(sample);
        std::unique_lock lock(mutex);
        if (changed.wait_for(lock, std::chrono::milliseconds(100), [&] { return received; })) break;
    }
    std::lock_guard lock(mutex);
    if (!received || client.Configuration().Topics().size() != 1) return 1;
    std::cout << options.DomainId() << '\n';
}
