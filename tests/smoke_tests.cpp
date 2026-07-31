#include "dds_cpp/dds_cpp.hpp"

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

namespace {

using namespace std::chrono_literals;

void require(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

void set_environment(const char* name, const char* value)
{
#if defined(_WIN32)
    _putenv_s(name, value == nullptr ? "" : value);
#else
    if (value == nullptr) unsetenv(name);
    else setenv(name, value, 1);
#endif
}

std::filesystem::path find_repo_root()
{
    auto current = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::is_directory(current / "DDS_Nuget" / "definitions")) return current;
        if (!current.has_parent_path() || current.parent_path() == current) break;
        current = current.parent_path();
    }
    throw std::runtime_error("Could not find DDSClient repository root.");
}

dds_cpp::DdsClientOptions in_memory_options(const std::filesystem::path& definitions)
{
    dds_cpp::DdsClientOptions options;
    options.topics_xml_path = (definitions / "topics.xml").string();
    options.qos_profiles_xml_path = (definitions / "qos_profiles.xml").string();
    options.dds_sim_xml_path = (definitions / "DDSSim.xml").string();
    return options;
}

struct Tick final {
    std::uint32_t frame{};
    double time{};
};

namespace parity {
struct TimeTickInformation final {
    std::uint64_t time_tick{};
};
} // namespace parity

void test_configuration(const std::filesystem::path& definitions)
{
    const auto configuration = dds_cpp::DdsConfigurationLoader::load(in_memory_options(definitions));
    require(configuration.topics().size() == 16, "Expected 16 configured topics.");
    require(configuration.try_get_topic("TimeTickInformation") != nullptr, "TimeTickInformation is missing.");

    const auto file_options = dds_cpp::DdsClientOptions::load((definitions / "dds_client.asap.xml").string());
    require(file_options.use_rti_transport, "Rti transport was not loaded from dds_client.asap.xml.");
    require(file_options.participant_name == "DdsAmbassadorClient", "Participant name was not loaded.");
    require(file_options.initial_peers.size() == 1, "Initial peer was not loaded.");
    require(std::filesystem::path(file_options.topics_xml_path).is_absolute(), "Definition path was not resolved.");
}

void test_in_memory(const std::filesystem::path& definitions)
{
    std::unique_ptr<dds_cpp::Subscription> subscription;
    {
        auto client = dds_cpp::DdsClient::connect(in_memory_options(definitions));
        bool received = false;
        Tick received_tick{};
        subscription = client.subscribe<Tick>("TimeTickInformation", [&](const Tick& tick) {
            received = true;
            received_tick = tick;
        });
        client.publish("TimeTickInformation", Tick{42U, 12.5});
        require(received, "In-memory sample was not received.");
        require(received_tick.frame == 42U && received_tick.time == 12.5, "In-memory sample was corrupted.");
    }

    // A subscription is allowed to outlive its client and must not dereference a destroyed transport.
    subscription.reset();
}

void test_in_memory_rejects_conflicting_topic_type(const std::filesystem::path& definitions)
{
    auto client = dds_cpp::DdsClient::connect(in_memory_options(definitions));
    auto subscription = client.subscribe<Tick>("TimeTickInformation", [](const Tick&) {});
    bool threw = false;
    try {
        client.publish("TimeTickInformation", parity::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException&) {
        threw = true;
    }
    require(threw, "In-memory topic accepted two different native sample types.");
}

void test_qos_library_name_is_validated(const std::filesystem::path& definitions)
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("ddscpp-qos-library-" + std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto qos = directory / "qos_profiles.xml";
    {
        std::ofstream output(qos);
        output << "<dds><qos_library name=\"WrongLibrary\">"
                  "<qos_profile name=\"ReliableRealtime\"/>"
                  "</qos_library></dds>";
    }

    auto options = in_memory_options(definitions);
    options.qos_profiles_xml_path = qos.string();
    bool threw = false;
    try {
        (void)dds_cpp::DdsConfigurationLoader::load(options);
    } catch (const dds_cpp::DdsConfigurationException& exception) {
        threw = std::string(exception.what()).find("AmbassadorProfiles") != std::string::npos;
    }
    std::filesystem::remove_all(directory);
    require(threw, "A QoS profile from the wrong library was accepted.");
}

void test_configuration_root_is_validated(const std::filesystem::path& definitions)
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("ddscpp-root-validation-" + std::to_string(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto topics = directory / "topics.xml";
    {
        std::ofstream output(topics);
        output << "<wrong><topic name=\"TimeTickInformation\" "
                  "qos_profile=\"ReliableRealtime\" direction=\"Both\"/></wrong>";
    }

    bool threw = false;
    try {
        (void)dds_cpp::DdsConfigurationLoader::load(
            topics.string(), (definitions / "qos_profiles.xml").string(),
            (definitions / "DDSSim.xml").string());
    } catch (const dds_cpp::DdsConfigurationException&) {
        threw = true;
    }
    const auto config = directory / "dds_client.xml";
    {
        std::ofstream output(config);
        output << "<wrong><transport>InMemory</transport></wrong>";
    }
    bool config_threw = false;
    try {
        (void)dds_cpp::DdsClientOptions::load(config.string());
    } catch (const dds_cpp::DdsConfigurationException&) {
        config_threw = true;
    }
    {
        std::ofstream output(topics);
        output << "<topics><topic name=\"TimeTickInformation\" "
                  "qos_profile=\"ReliableRealtime\" direction=\"Both\"/><!--";
    }
    bool comment_threw = false;
    try {
        (void)dds_cpp::DdsConfigurationLoader::load(
            topics.string(), (definitions / "qos_profiles.xml").string(),
            (definitions / "DDSSim.xml").string());
    } catch (const dds_cpp::DdsConfigurationException&) {
        comment_threw = true;
    }
    std::filesystem::remove_all(directory);
    require(threw, "A topics.xml file with the wrong root element was accepted.");
    require(config_threw, "A DDS client config with the wrong root element was accepted.");
    require(comment_threw, "An unterminated XML comment was accepted.");
}

void test_close_is_thread_safe(const std::filesystem::path& definitions)
{
    auto client = dds_cpp::DdsClient::connect(in_memory_options(definitions));
    std::atomic_bool stop{false};
    std::thread worker([&] {
        while (!stop.load()) {
            (void)client.is_open();
            try {
                client.publish("TimeTickInformation", Tick{});
            } catch (const dds_cpp::DdsOperationException&) {
            }
        }
    });
    client.close();
    stop = true;
    worker.join();
    require(!client.is_open(), "Concurrent close left the client open.");
}

void test_nuget_api_parity(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    auto client = dds_cpp::DdsClient::Connect(options);

    bool received = false;
    auto subscription = client.Subscribe<parity::TimeTickInformation>(
        [&](const parity::TimeTickInformation& sample) {
            received = sample.time_tick == 1234ULL;
        });

    auto publisher = client.CreatePublisher<parity::TimeTickInformation>();
    require(publisher.Topic().name == "TimeTickInformation", "Publisher topic does not match NuGet API.");
    require(
        publisher.Topic().QualifiedQosProfile() == "AmbassadorProfiles::ReliableRealtime",
        "QualifiedQosProfile does not match NuGet API.");

    parity::TimeTickInformation sample{1234ULL};
    publisher.Publish(sample);
    require(received, "NuGet-style CreatePublisher/Subscribe API did not deliver the sample.");

    received = false;
    client.Publish(sample);
    require(received, "NuGet-style type-only Publish API did not resolve the topic.");

    const dds_cpp::TopicDefinition* topic = nullptr;
    require(client.Configuration().TryGetTopic("TimeTickInformation", topic), "TryGetTopic returned false.");
    require(topic != nullptr && client.Configuration().GetTopic(topic->name).name == topic->name, "GetTopic mismatch.");
}

void test_close_and_reconnect(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    auto client = dds_cpp::DdsClient::Connect(options);
    auto publisher = client.CreatePublisher<parity::TimeTickInformation>();
    client.Dispose();
    require(!client.is_open(), "Disposed C++ client still reports open.");

    bool client_threw = false;
    try {
        client.Publish(parity::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        client_threw = std::string(exception.what()).find("closed") != std::string::npos;
    }
    require(client_threw, "Disposed C++ client did not reject publish.");

    bool publisher_threw = false;
    try {
        publisher.Publish(parity::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        publisher_threw = std::string(exception.what()).find("closed") != std::string::npos;
    }
    require(publisher_threw, "Publisher outlived its closed client.");

    auto reconnected = dds_cpp::DdsClient::Connect(options);
    bool received = false;
    auto subscription = reconnected.Subscribe<parity::TimeTickInformation>(
        [&](const parity::TimeTickInformation&) { received = true; });
    reconnected.Publish(parity::TimeTickInformation{});
    require(received, "Reconnected C++ client did not deliver a sample.");
}

void test_direction_policy_matches_nuget(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    std::vector<dds_cpp::TopicDefinition> topics{
        {"TimeTickInformation", "ReliableRealtime", dds_cpp::TopicDirection::Subscribe}};
    dds_cpp::DdsClient client(
        options,
        dds_cpp::DdsConfiguration(std::move(topics)),
        std::make_unique<dds_cpp::InMemoryDdsTransport>());

    bool threw = false;
    try {
        client.Publish(parity::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        threw = std::string(exception.what()).find("cannot publish") != std::string::npos;
    }
    require(threw, "Subscribe-only topic did not reject publish like the NuGet client.");
}

void test_invalid_transport_environment_matches_nuget(const std::filesystem::path& definitions)
{
    const auto* current = std::getenv("DDS_USE_RTI_TRANSPORT");
    const std::string original = current == nullptr ? std::string{} : current;
    const bool existed = current != nullptr;
    set_environment("DDS_USE_RTI_TRANSPORT", "maybe");
    bool threw = false;
    try {
        const auto options = dds_cpp::DdsClientOptions::Load();
        (void)options;
    } catch (const dds_cpp::DdsConfigurationException& exception) {
        threw = std::string(exception.what()).find("DDS_USE_RTI_TRANSPORT") != std::string::npos;
    }
    set_environment("DDS_USE_RTI_TRANSPORT", existed ? original.c_str() : nullptr);
    require(threw, "Invalid DDS_USE_RTI_TRANSPORT did not match the NuGet exception contract.");
}

void test_path_precedence_matches_nuget(const std::filesystem::path& definitions)
{
    const auto* current_topics = std::getenv("DDS_TOPICS_XML_PATH");
    const auto* current_qos = std::getenv("DDS_QOS_PROFILES_XML_PATH");
    const auto* current_sim = std::getenv("DDS_DDSSIM_XML_PATH");
    const std::string original_topics = current_topics == nullptr ? "" : current_topics;
    const std::string original_qos = current_qos == nullptr ? "" : current_qos;
    const std::string original_sim = current_sim == nullptr ? "" : current_sim;

    set_environment("DDS_TOPICS_XML_PATH", "missing-topics.xml");
    set_environment("DDS_QOS_PROFILES_XML_PATH", "missing-qos.xml");
    set_environment("DDS_DDSSIM_XML_PATH", "missing-sim.xml");
    const auto explicit_configuration = dds_cpp::DdsConfigurationLoader::load(in_memory_options(definitions));
    require(explicit_configuration.topics().size() == 16, "Explicit Options did not override environment paths.");

    dds_cpp::DdsClientOptions direct_options;
    const auto direct_configuration = dds_cpp::DdsConfigurationLoader::load(direct_options);
    require(direct_configuration.topics().size() == 16, "Direct Options unexpectedly read environment paths.");

    set_environment("DDS_TOPICS_XML_PATH", current_topics == nullptr ? nullptr : original_topics.c_str());
    set_environment("DDS_QOS_PROFILES_XML_PATH", current_qos == nullptr ? nullptr : original_qos.c_str());
    set_environment("DDS_DDSSIM_XML_PATH", current_sim == nullptr ? nullptr : original_sim.c_str());
}

void test_relative_environment_paths_use_config_directory()
{
    const auto directory = std::filesystem::temp_directory_path() / "ddscpp-relative-config-test";
    std::filesystem::create_directories(directory);
    const auto config = directory / "client.xml";
    {
        std::ofstream output(config);
        output << "<dds_client><transport>InMemory</transport>"
                  "<participant_name>A &amp; B</participant_name>"
                  "<peer>outside-is-not-a-setting</peer>"
                  "<initial_peers><peer>inside-peer</peer></initial_peers>"
                  "</dds_client>";
    }

    const auto* current_topics = std::getenv("DDS_TOPICS_XML_PATH");
    const std::string original_topics = current_topics == nullptr ? "" : current_topics;
    set_environment("DDS_TOPICS_XML_PATH", "topics.xml");
    const auto options = dds_cpp::DdsClientOptions::Load(config.string());
    set_environment("DDS_TOPICS_XML_PATH", current_topics == nullptr ? nullptr : original_topics.c_str());

    require(
        std::filesystem::path(options.TopicsXmlPath()) == (directory / "topics.xml").lexically_normal(),
        "Relative environment paths were not resolved from the config directory.");
    require(
        options.InitialPeers().size() == 1 && options.InitialPeers().front() == "inside-peer",
        "Peers outside initial_peers were incorrectly loaded.");
    require(options.ParticipantName() == "A & B", "XML entities were not decoded in C++ options.");
    std::filesystem::remove_all(directory);
}

#if defined(DDSCPP_ENABLE_RTI)
void test_rti(const std::filesystem::path& definitions)
{
    auto subscriber_options = in_memory_options(definitions);
    subscriber_options.use_rti_transport = true;
    subscriber_options.domain_id = 173;
    subscriber_options.participant_name = "DDSCPP-Test-Subscriber";
    subscriber_options.initial_peers.clear();

    auto publisher_options = subscriber_options;
    publisher_options.participant_name = "DDSCPP-Test-Publisher";

    auto subscriber = dds_cpp::DdsClient::connect(subscriber_options);
    auto publisher = dds_cpp::DdsClient::connect(publisher_options);

    std::mutex mutex;
    std::condition_variable changed;
    bool received = false;
    std::uint64_t received_tick = 0;
    std::unique_ptr<dds_cpp::Subscription> subscription;
    subscription = subscriber.subscribe<MSG::TimeTickInformation>(
        "TimeTickInformation",
        [&](const MSG::TimeTickInformation& sample) {
            subscription.reset();
            std::lock_guard lock(mutex);
            received_tick = sample.TimeTickMessage().TimeTick();
            received = true;
            changed.notify_all();
        });

    publisher.create_publisher<MSG::TimeTickInformation>("TimeTickInformation");
    std::this_thread::sleep_for(1500ms);

    MSG::TimeTickInformation sample;
    sample.TimeTickMessage().TimeTick(987654321ULL);
    sample.TimeTickMessage().SyncCycle(7U);

    for (int attempt = 0; attempt < 10; ++attempt) {
        publisher.publish("TimeTickInformation", sample);
        std::unique_lock lock(mutex);
        if (changed.wait_for(lock, 300ms, [&] { return received; })) break;
    }

    require(received, "RTI sample was not received by the second participant.");
    require(received_tick == 987654321ULL, "RTI sample payload was corrupted.");
}
#endif

} // namespace

int main()
{
    try {
        const auto definitions = find_repo_root() / "DDS_Nuget" / "definitions";
        test_configuration(definitions);
        test_in_memory(definitions);
        test_in_memory_rejects_conflicting_topic_type(definitions);
        test_qos_library_name_is_validated(definitions);
        test_configuration_root_is_validated(definitions);
        test_close_is_thread_safe(definitions);
        test_nuget_api_parity(definitions);
        test_close_and_reconnect(definitions);
        test_direction_policy_matches_nuget(definitions);
        test_invalid_transport_environment_matches_nuget(definitions);
        test_path_precedence_matches_nuget(definitions);
        test_relative_environment_paths_use_config_directory();
#if defined(DDSCPP_ENABLE_RTI)
        test_rti(definitions);
#endif
        std::cout << "DDSCPP configuration, in-memory, lifetime, and RTI tests passed.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << '\n';
        return 1;
    }
}
