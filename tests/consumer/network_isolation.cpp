#include "dds_cpp/DdsCpp.hpp"
#include "NetworkA_messages.hpp"
#include "NetworkB_messages.hpp"
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <stdexcept>

namespace A = DdsModules_NetworkA::Common;
namespace B = DdsModules_NetworkB::Common;
using namespace std::chrono_literals;

void test_network_isolation()
{
    const std::string root = DDSCLIENT_TEST_DEFINITIONS;
    const auto path_a = root + "/networks/NetworkA.qos.xml";
    const auto path_b = root + "/networks/NetworkB.qos.xml";
    dds_cpp::DdsNetwork a("NetworkA", dds_cpp::NetworkAMessages(), 96, path_a, "Network::Participant", "Network::Data");
    dds_cpp::DdsNetwork b("NetworkB", dds_cpp::NetworkBMessages(), 97, path_b, "Network::Participant", "Network::Data");
    const auto options_a = a.CreateClientOptions();
    const auto options_b = b.CreateClientOptions();
    if (options_a.Topics()->front().QualifiedQosProfile() != "Network::Data") throw std::runtime_error("Wrong topic profile.");
    const auto explicit_qos = a.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Named<A::Status>("Status", dds_cpp::TopicDirection::Both, "ReliableRealtime")});
    if (explicit_qos.Topics()->front().QualifiedQosProfile() != "Network::ReliableRealtime")
        throw std::runtime_error("Explicit QoS profile was overwritten.");
    bool rejected = false;
    try { (void)a.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{dds_cpp::Topic::Named<A::Status>("Status", dds_cpp::TopicDirection::Both, "Other::Data")}); }
    catch (const dds_cpp::DdsConfigurationException&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Cross-network QoS profile accepted.");
    if (dds::topic::topic_type_name<A::Status>::value() != "Common::Status" ||
        dds::topic::topic_type_name<B::Status>::value() != "Common::Status") throw std::runtime_error("Wire name changed.");
    dds::core::QosProvider qos_a(path_a), qos_b(path_b);
    if (qos_a.datareader_qos("Network::Data").policy<rti::core::policy::TransportMulticast>().settings()[0].receive_address() != "239.255.7.11" ||
        qos_b.datareader_qos("Network::Data").policy<rti::core::policy::TransportMulticast>().settings()[0].receive_address() != "239.255.7.12")
        throw std::runtime_error("QoS providers leaked same-named profiles.");
    rejected = false;
    try {
        auto invalid = a.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{
            dds_cpp::Topic::Named<A::Status>("Status", dds_cpp::TopicDirection::Both, "NoMulticast")},
            "239.255.7.20");
        auto client = dds_cpp::DdsClient::Connect(invalid);
    } catch (const dds_cpp::DdsConfigurationException&) { rejected = true; }
    if (!rejected) throw std::runtime_error("Selected reader without multicast was accepted.");
    std::mutex gate;
    std::condition_variable changed;
    std::vector<std::uint64_t> values_a, values_b;
    auto receiver_a = dds_cpp::DdsClient::Connect(options_a);
    auto receiver_b = dds_cpp::DdsClient::Connect(options_b);
    auto sub_a = receiver_a.Subscribe<A::Status>([&](const auto& sample) {
        std::lock_guard lock(gate);
        if (sample.Healthy() && sample.Mode() == A::Mode::Active) values_a.push_back(sample.Counter());
        changed.notify_all();
    });
    auto sub_b = receiver_b.Subscribe<B::Status>([&](const auto& sample) {
        std::lock_guard lock(gate);
        if (sample.Description() == "network-b" && sample.Mode() == B::Mode::Active) values_b.push_back(sample.Counter());
        changed.notify_all();
    });
    auto sender_a = dds_cpp::DdsClient::Connect(options_a);
    auto pub_a = sender_a.CreatePublisher<A::Status>();
    pub_a.WaitForSubscribers(1, 15s);
    for (std::uint64_t round = 0; round < 2; ++round) {
        auto sender_b = dds_cpp::DdsClient::Connect(options_b);
        auto pub_b = sender_b.CreatePublisher<B::Status>();
        pub_b.WaitForSubscribers(1, 15s);
        for (auto i = round * 4 + 1; i <= (round + 1) * 4; ++i) {
            A::Status sa; sa.Counter(i); sa.Healthy(true); sa.Mode(A::Mode::Active); pub_a.Publish(sa);
            B::Status sb; sb.Counter(1000 + i); sb.Description("network-b"); sb.Mode(B::Mode::Active); pub_b.Publish(sb);
        }
        pub_a.WaitForAcknowledgments(10s); pub_b.WaitForAcknowledgments(10s);
        const auto count = (round + 1) * 4;
        std::unique_lock lock(gate);
        if (!changed.wait_for(lock, 10s, [&] { return values_a.size() >= count && values_b.size() >= count; }))
            throw std::runtime_error("Network delivery timed out.");
        if (values_a.size() != count || values_b.size() != count) throw std::runtime_error("Duplicate data.");
        for (std::size_t i = 0; i < count; ++i)
            if (values_a[i] != i + 1 || values_b[i] != i + 1001) throw std::runtime_error("Mixed/lost/reordered data.");
    }
    std::cout << "Same Common::Status: 8+8 UDP samples, separate profiles, reconnect and wire names passed.\n";
}
