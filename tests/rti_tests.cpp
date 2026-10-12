#include "test_support.hpp"
#include "Telemetry_messages.hpp"
#include "RtiTransportResources.hpp"
#include "RtiReaderQosOverride.hpp"
#include <future>

namespace dds_test {
namespace {

void test_public_status_and_errors(const std::filesystem::path& definitions)
{
    dds_cpp::DdsNetwork network("status-test", dds_cpp::DDSSimMessages(), 107,
        (definitions / "networks/NetworkA.qos.xml").string(), "Network::Participant", "Network::Data");
    auto options = network.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{dds_cpp::Topic::Both<MSG::TimeTickInformation>()});
    options.ShutdownTimeout() = 30ms;
    auto client = dds_cpp::DdsClient::Connect(options);
    require(client.ShutdownState() == dds_cpp::DdsShutdownState::Open && !client.WaitForShutdown(0ms), "Open status must not report cleanup complete.");
    std::promise<void> observed;
    auto error = observed.get_future();
    client.SetSubscriptionErrorHandler([&](const auto& failure) {
        require(!failure.IsFatal && failure.TopicName == "TimeTickInformation" && failure.SubscriptionId != 0,
            "Wrong subscription error metadata.");
        observed.set_value();
        throw std::runtime_error("Observer errors must be isolated");
    });
    auto reader = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("handler failure"); });
    auto writer = client.CreatePublisher<MSG::TimeTickInformation>();
    writer.WaitForSubscribers(1, 5s);
    writer.Publish(MSG::TimeTickInformation{});
    require(error.wait_for(5s) == std::future_status::ready, "Handler error was not reported.");
    require(client.LastSubscriptionError().has_value(), "Last error was not retained.");
    client.Close();
    require(client.WaitForShutdown(5s) && client.ShutdownState() == dds_cpp::DdsShutdownState::Closed,
        "Native cleanup completion not observable with retained subscription handle.");
    require(!client.ShutdownError(), "Unexpected shutdown failure.");
}

void test_broken_native_error_output(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.DomainId() = 109;
    options.LogLevel() = dds_cpp::DdsLogLevel::Error;
    auto client = dds_cpp::DdsClient::Connect(options);
    auto transport = dds_cpp::detail::ClientAccess::RetainTransport(client);
    std::atomic<int> received{0};
    auto reader = transport->Subscribe(client.Configuration().GetTopic(typeid(MSG::TimeTickInformation)),
        typeid(MSG::TimeTickInformation), [&](const void*) {
            ++received;
            throw std::runtime_error("네이티브 핸들러 오류 주입");
        }, 1);
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    publisher.WaitForSubscribers(1, 5s);
    {
        BrokenOutput output(std::cerr);
        publisher.Publish(MSG::TimeTickInformation{});
        publisher.Publish(MSG::TimeTickInformation{});
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (received < 2 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(10ms);
        reader.reset(); // 출력 스트림은 콜백 스레드 종료까지 유지한다.
    }
    require(received == 2, "오류 로그 실패가 네이티브 수신 스레드를 중단했습니다.");
}

void test_close_requests_all_readers_before_shared_deadline()
{
    struct DelayedReader final : dds_cpp::detail::ISubscriptionDispatcher {
        bool requested{false};
        bool all_requested_when_waited{false};
        std::chrono::steady_clock::time_point seen_deadline{};
        std::function<bool()> all_requested;
        void RequestStop() noexcept override { requested = true; }
        void WaitForShutdown(std::chrono::steady_clock::time_point deadline) noexcept override {
            all_requested_when_waited = all_requested();
            seen_deadline = deadline;
            std::this_thread::sleep_until(deadline);
        }
    };
    std::vector<std::shared_ptr<DelayedReader>> owners;
    std::vector<std::weak_ptr<dds_cpp::detail::ISubscriptionDispatcher>> readers;
    for (int i = 0; i < 3; ++i) {
        auto reader = std::make_shared<DelayedReader>();
        reader->all_requested = [&] {
            return std::all_of(owners.begin(), owners.end(), [](const auto& item) { return item->requested; });
        };
        readers.push_back(reader);
        owners.push_back(reader);
    }
    const auto started = std::chrono::steady_clock::now();
    dds_cpp::detail::StopSubscriptions(readers, 100ms);
    for (const auto& reader : owners) {
        require(reader->all_requested_when_waited, "Wait began before all readers were stopped.");
        require(reader->seen_deadline == owners.front()->seen_deadline, "Readers received independent timeout budgets.");
    }
    require(std::chrono::steady_clock::now() - started < 500ms, "Shared shutdown deadline was not bounded.");
}

void test_multicast_override_uses_parsed_qos(const std::filesystem::path& definitions)
{
    struct RestoreEnvironment {
        std::string name;
        std::optional<std::string> value;
        explicit RestoreEnvironment(const char* key) : name(key) {
            if (const auto* current = std::getenv(key)) value = current;
        }
        ~RestoreEnvironment() { set_environment(name.c_str(), value ? value->c_str() : nullptr); }
    } general("DDS_MULTICAST_ADDRESS"), receive("DDS_MULTICAST_RECEIVE_ADDRESS");
    set_environment("DDS_MULTICAST_ADDRESS", nullptr);
    set_environment("DDS_MULTICAST_RECEIVE_ADDRESS", "239.255.5.99");
    const auto file = std::filesystem::temp_directory_path() /
        ("dds-override-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
    struct RemoveFile {
        std::filesystem::path path;
        ~RemoveFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } remove{file};
    std::ifstream input(definitions / "qos_profiles.xml");
    std::string xml((std::istreambuf_iterator<char>(input)), {});
    auto position = xml.find("<receive_address>");
    require(position != std::string::npos, "Fixture has no multicast address.");
    xml.replace(position, std::string("<receive_address>").size(), "<receive_address >");
    position = xml.find("</qos_library>");
    xml.insert(position, "<qos_profile name='Inherited' base_name='AmbassadorProfiles::ReliableRealtime'/>");
    { std::ofstream output(file); output << xml; }
    {
        dds_cpp::detail::RtiReaderQosOverride effective(file.string(), "239.255.5.99");
        dds::core::QosProvider provider(effective.path());
        effective.validate(provider);
        for (const auto* name : {"AmbassadorProfiles::ReliableRealtime", "AmbassadorProfiles::Inherited"}) {
            const auto qos = effective.ReaderQos(provider, name);
            const auto settings = qos.policy<rti::core::policy::TransportMulticast>().settings();
            require(!settings.empty(), "Override lost multicast settings.");
            for (const auto& setting : settings)
                require(setting.receive_address() == "239.255.5.99", "Parsed QoS override was not applied.");
        }
        std::ifstream unchanged(file);
        require(std::string((std::istreambuf_iterator<char>(unchanged)), {}) == xml, "Override modified the source XML.");
    }
    { std::ofstream output(file); output << "<dds><qos_library name='AmbassadorProfiles'><qos_profile name='ReliableRealtime'/></qos_library></dds>"; }
    {
        dds_cpp::detail::RtiReaderQosOverride effective(file.string(), "239.255.5.99");
        dds::core::QosProvider provider(effective.path());
        bool rejected = false;
        try { effective.validate(provider); }
        catch (const dds_cpp::DdsConfigurationException&) { rejected = true; }
        require(rejected, "Override silently accepted profiles with no multicast settings.");
    }
}

void test_first_response_published_from_handler(const std::filesystem::path& definitions)
{
    // Both peers run on loopback. No Response writer or topic is created on the
    // server before the request arrives: this used to fail inside RTI's listener EA.
    dds_cpp::DdsNetwork network("response-regression", dds_cpp::DDSSimMessages(), 106,
        (definitions / "networks/NetworkA.qos.xml").string(), "Network::Participant", "Network::Data");
    auto server = dds_cpp::DdsClient::Connect(network.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("Request", dds_cpp::TopicDirection::Subscribe),
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("Response", dds_cpp::TopicDirection::Publish)}));
    auto peer = dds_cpp::DdsClient::Connect(network.CreateClientOptions(std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("Request", dds_cpp::TopicDirection::Publish),
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("Response", dds_cpp::TopicDirection::Subscribe)}));
    std::atomic<int> received{0}, errors{0};
    auto response_sub = peer.Subscribe<MSG::TimeTickInformation>([&](const auto&) { ++received; }, "Response");
    auto request_sub = server.Subscribe<MSG::TimeTickInformation>([&](const auto& sample) {
        try {
            auto response = server.CreatePublisher<MSG::TimeTickInformation>("Response");
            response.WaitForSubscribers(1, 5s);
            response.Publish(sample);
        } catch (...) { ++errors; }
    }, "Request");
    auto request = peer.CreatePublisher<MSG::TimeTickInformation>("Request");
    request.WaitForSubscribers(1, 5s);
    for (int i = 0; i < 4; ++i) request.Publish(MSG::TimeTickInformation{});
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (received < 4 && errors == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    require(errors == 0, "First response publish failed inside a subscription handler.");
    require(received == 4, "Request/response burst lost messages.");
}

void test_independent_message_modules(const std::filesystem::path& definitions)
{
    auto stable_options = in_memory_options(definitions);
    stable_options.DomainId() = 83;
    stable_options.MulticastReceiveAddress() = "239.255.5.83";
    stable_options.Topics() = std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("SharedTopic")};
    auto stable = dds_cpp::DdsClient::Connect(stable_options);
    std::atomic<int> received{0};
    auto listener = stable.Subscribe<MSG::TimeTickInformation>([&](const auto&) { ++received; });
    auto writer = stable.CreatePublisher<MSG::TimeTickInformation>();
    writer.WaitForSubscribers(1, 10s);
    for (int i = 1; i <= 3; ++i) {
        auto other_options = in_memory_options(definitions);
        other_options.MessageModules() = {dds_cpp::TelemetryMessages()};
        other_options.DomainId() = 84;
        other_options.MulticastReceiveAddress() = "239.255.5.84";
        other_options.Topics() = std::vector<dds_cpp::TopicDefinition>{
            dds_cpp::Topic::Named<Telemetry::Heartbeat>("SharedTopic")};
        {
            auto other = dds_cpp::DdsClient::Connect(other_options);
            std::atomic<bool> arrived{false};
            auto sub = other.Subscribe<Telemetry::Heartbeat>([&](const auto& sample) {
                if (sample.Counter() == static_cast<std::uint64_t>(i)) arrived = true;
            });
            auto publisher = other.CreatePublisher<Telemetry::Heartbeat>();
            publisher.WaitForSubscribers(1, 10s);
            Telemetry::Heartbeat sample;
            sample.Counter(i);
            publisher.Publish(sample);
            for (int attempt = 0; attempt < 250 && !arrived; ++attempt) std::this_thread::sleep_for(20ms);
            require(arrived, "Second schema module did not receive.");
        }
        writer.Publish(MSG::TimeTickInformation{});
    }
    writer.WaitForAcknowledgments(10s);
    for (int attempt = 0; attempt < 250 && received != 3; ++attempt) std::this_thread::sleep_for(20ms);
    require(received == 3, "Closing another schema/network interrupted the stable connection.");

    auto mixed_options = in_memory_options(definitions);
    mixed_options.MessageModules().push_back(dds_cpp::TelemetryMessages());
    mixed_options.Topics() = std::vector<dds_cpp::TopicDefinition>{
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("ClockA"),
        dds_cpp::Topic::Named<MSG::TimeTickInformation>("ClockB"),
        dds_cpp::Topic::Named<Telemetry::Heartbeat>("Status")};
    auto mixed = test_client(mixed_options);
    bool ambiguous = false;
    try { (void)mixed.CreatePublisher<MSG::TimeTickInformation>(); }
    catch (const dds_cpp::DdsConfigurationException&) { ambiguous = true; }
    require(ambiguous, "Ambiguous type binding was accepted.");
    (void)mixed.CreatePublisher<MSG::TimeTickInformation>("ClockA");
    (void)mixed.CreatePublisher<Telemetry::Heartbeat>("Status");
}

void test_two_udp_network_bursts(const std::filesystem::path& definitions)
{
    const auto file = std::filesystem::temp_directory_path() /
        ("dds-two-networks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
    struct RemoveFile {
        std::filesystem::path path;
        ~RemoveFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } remove{file};
    std::ifstream input(definitions / "qos_profiles.xml");
    std::string xml((std::istreambuf_iterator<char>(input)), {});
    const auto end = xml.find("</domain_participant_qos>");
    require(end != std::string::npos, "Missing participant QoS in test fixture.");
    xml.insert(end, "<transport_builtin><mask>UDPv4</mask></transport_builtin>");
    const std::string shmem = "<element>builtin.shmem://</element>";
    const auto peer = xml.find(shmem);
    if (peer != std::string::npos) xml.erase(peer, shmem.size());
    { std::ofstream output(file); output << xml; }
    auto a = in_memory_options(definitions);
    a.DomainId() = 88;
    a.QosProfilesXmlPath() = file.string();
    a.MulticastReceiveAddress() = "239.255.5.88";
    a.Topics() = std::vector<dds_cpp::TopicDefinition>{dds_cpp::Topic::Named<MSG::TimeTickInformation>("SharedTopic")};
    auto b = a;
    b.DomainId() = 89;
    b.MulticastReceiveAddress() = "239.255.5.89";
    b.MessageModules() = {dds_cpp::TelemetryMessages()};
    b.Topics() = std::vector<dds_cpp::TopicDefinition>{dds_cpp::Topic::Named<Telemetry::Heartbeat>("SharedTopic")};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::vector<std::uint64_t>> received_a(3), received_b(3);
    std::vector<dds_cpp::DdsClient> receivers;
    std::vector<std::unique_ptr<dds_cpp::Subscription>> subscriptions;
    for (int i = 0; i < 3; ++i) {
        receivers.push_back(dds_cpp::DdsClient::Connect(a));
        subscriptions.push_back(receivers.back().Subscribe<MSG::TimeTickInformation>([&, i](const auto& sample) {
            std::lock_guard lock(mutex); received_a[i].push_back(sample.TimeTickMessage().TimeTick()); changed.notify_all();
        }));
        receivers.push_back(dds_cpp::DdsClient::Connect(b));
        subscriptions.push_back(receivers.back().Subscribe<Telemetry::Heartbeat>([&, i](const auto& sample) {
            std::lock_guard lock(mutex); received_b[i].push_back(sample.Counter()); changed.notify_all();
        }));
    }
    auto sender_a = dds_cpp::DdsClient::Connect(a);
    auto writer_a = sender_a.CreatePublisher<MSG::TimeTickInformation>();
    writer_a.WaitForSubscribers(3, 15s);
    for (std::uint64_t round = 0; round < 2; ++round) {
        auto sender_b = dds_cpp::DdsClient::Connect(b);
        auto writer_b = sender_b.CreatePublisher<Telemetry::Heartbeat>();
        writer_b.WaitForSubscribers(3, 15s);
        std::promise<void> start;
        auto ready = start.get_future().share();
        auto send_a = std::async(std::launch::async, [&] {
            ready.wait();
            for (auto value = round * 4 + 1; value <= (round + 1) * 4; ++value) {
                MSG::TimeTickInformation sample; sample.TimeTickMessage().TimeTick(value); writer_a.Publish(sample);
            }
            writer_a.WaitForAcknowledgments(10s);
        });
        auto send_b = std::async(std::launch::async, [&] {
            ready.wait();
            for (auto value = round * 4 + 1; value <= (round + 1) * 4; ++value) {
                Telemetry::Heartbeat sample; sample.Counter(1000 + value); writer_b.Publish(sample);
            }
            writer_b.WaitForAcknowledgments(10s);
        });
        start.set_value();
        send_a.get(); send_b.get();
        const auto count = (round + 1) * 4;
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, 10s, [&] {
            return std::all_of(received_a.begin(), received_a.end(), [&](const auto& v) { return v.size() >= count; }) &&
                std::all_of(received_b.begin(), received_b.end(), [&](const auto& v) { return v.size() >= count; });
        }), "Two-domain UDP burst did not reach all six independent readers.");
        for (int reader = 0; reader < 3; ++reader) {
            require(received_a[reader].size() == count && received_b[reader].size() == count, "Unexpected duplicate/cross-domain samples.");
            for (std::size_t index = 0; index < count; ++index) {
                require(received_a[reader][index] == index + 1, "Simulation sample lost or reordered.");
                require(received_b[reader][index] == index + 1001, "Telemetry sample lost or reordered.");
            }
        }
    }
    std::cout << "Two UDP domains: 3 readers each, 8 samples each, 48 deliveries verified.\n";
}

void test_rti_burst(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.DomainId() = 75;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::vector<std::uint64_t>> received(3);
    auto sender = dds_cpp::DdsClient::Connect(options);
    auto publisher = sender.CreatePublisher<MSG::TimeTickInformation>();
    bool timed_out = false;
    try { publisher.WaitForSubscribers(1, 100ms); }
    catch (const dds_cpp::DdsOperationException&) { timed_out = true; }
    require(timed_out, "Missing subscribers did not time out.");
    bool rejected = false;
    try { publisher.WaitForSubscribers(0, 100ms); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Zero subscriber count was accepted.");
    std::vector<dds_cpp::DdsClient> clients;
    std::vector<std::unique_ptr<dds_cpp::Subscription>> subscriptions;
    for (int i = 0; i < 3; ++i) {
        clients.push_back(dds_cpp::DdsClient::Connect(options));
        subscriptions.push_back(clients.back().Subscribe<MSG::TimeTickInformation>(
            [&, i](const MSG::TimeTickInformation& sample) {
                std::lock_guard lock(mutex);
                received[i].push_back(sample.TimeTickMessage().TimeTick());
                changed.notify_all();
            }));
    }
    publisher.WaitForSubscribers(3, 10s);
    timed_out = false;
    try { publisher.WaitForSubscribers(4, 100ms); }
    catch (const dds_cpp::DdsOperationException&) { timed_out = true; }
    require(timed_out, "Wait accepted fewer than the required readers.");
    for (std::uint64_t i = 1; i <= 4; ++i) {
        MSG::TimeTickInformation sample;
        sample.TimeTickMessage().TimeTick(i);
        publisher.Publish(sample);
    }
    publisher.WaitForAcknowledgments(10s);
    sender.Close();
    {
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, 5s, [&] {
            return received[0].size() >= 4 && received[1].size() >= 4 && received[2].size() >= 4;
        }), "Three-reader burst timed out.");
        for (const auto& samples : received)
            require(samples == std::vector<std::uint64_t>{1, 2, 3, 4}, "Burst lost, duplicated or reordered samples.");
    }
    bool closed = false;
    try { publisher.WaitForAcknowledgments(100ms); }
    catch (const dds_cpp::DdsOperationException&) { closed = true; }
    require(closed, "Wait on closed publisher was accepted.");
}

void test_rti_close_from_callback(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.DomainId() = 78;
    std::atomic<bool> closed{false};
    auto receiver = dds_cpp::DdsClient::Connect(options);
    auto sender = dds_cpp::DdsClient::Connect(options);
    auto subscription = receiver.Subscribe<MSG::TimeTickInformation>([&](const auto&) {
        receiver.Close();
        closed = true;
    });
    auto publisher = sender.CreatePublisher<MSG::TimeTickInformation>();
    publisher.WaitForSubscribers(1, 10s);
    publisher.Publish(MSG::TimeTickInformation{});
    for (int i = 0; i < 100 && !closed; ++i) std::this_thread::sleep_for(20ms);
    require(closed, "Close from callback blocked its own reader cleanup.");
    subscription.reset();
}

void test_rti_close_stops_retained_subscription(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.DomainId() = 79;
    std::atomic<int> received{0};
    auto receiver = dds_cpp::DdsClient::Connect(options);
    auto sender = dds_cpp::DdsClient::Connect(options);
    auto subscription = receiver.Subscribe<MSG::TimeTickInformation>([&](const auto&) { ++received; });
    auto publisher = sender.CreatePublisher<MSG::TimeTickInformation>();
    publisher.WaitForSubscribers(1, 10s);
    receiver.Close();
    require(!receiver.IsOpen(), "Close did not close client.");
    for (int i = 0; i < 4; ++i) publisher.Publish(MSG::TimeTickInformation{});
    std::this_thread::sleep_for(300ms);
    require(received == 0, "Retained subscription delivered callbacks after Close.");
    subscription.reset();
}

void test_rti_same_client(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.DomainId() = 72;
    auto client = dds_cpp::DdsClient::Connect(options);
    std::mutex mutex;
    std::condition_variable changed;
    int received = 0;
    auto handler = [&](const MSG::TimeTickInformation& sample) {
        if (sample.TimeTickMessage().TimeTick() != 42) return;
        std::lock_guard lock(mutex);
        ++received;
        changed.notify_all();
    };
    auto first = client.Subscribe<MSG::TimeTickInformation>(handler);
    auto second = client.Subscribe<MSG::TimeTickInformation>(handler);
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    MSG::TimeTickInformation sample;
    sample.TimeTickMessage().TimeTick(42);
    auto await_samples = [&](int expected) {
        for (int attempt = 0; attempt < 30; ++attempt) {
            publisher.Publish(sample);
            std::unique_lock lock(mutex);
            if (changed.wait_for(lock, 100ms, [&] { return received >= expected; })) return;
        }
        throw std::runtime_error("Same-client RTI delivery failed.");
    };
    await_samples(2);
    first.reset();
    second.reset();
    // Removing readers must not close the shared topic used by the writer.
    auto third = client.Subscribe<MSG::TimeTickInformation>(handler);
    int next;
    { std::lock_guard lock(mutex); next = received + 1; }
    await_samples(next);
}

void test_rti(const std::filesystem::path& definitions)
{
    auto subscriber_options = in_memory_options(definitions);
    subscriber_options.DomainId() = 73;
    subscriber_options.ParticipantName() = "DDSCPP-Test-Subscriber";

    auto publisher_options = subscriber_options;
    publisher_options.ParticipantName() = "DDSCPP-Test-Publisher";

    auto subscriber = dds_cpp::DdsClient::Connect(subscriber_options);
    auto publisher = dds_cpp::DdsClient::Connect(publisher_options);

    std::mutex mutex;
    std::condition_variable changed;
    bool received = false;
    std::uint64_t received_tick = 0;
    std::unique_ptr<dds_cpp::Subscription> subscription;
    subscription = subscriber.Subscribe<MSG::TimeTickInformation>(
        [&](const MSG::TimeTickInformation& sample) {
            subscription.reset();
            std::lock_guard lock(mutex);
            received_tick = sample.TimeTickMessage().TimeTick();
            received = true;
            changed.notify_all();
        });

    auto typed_publisher = publisher.CreatePublisher<MSG::TimeTickInformation>();
    std::this_thread::sleep_for(1500ms);

    MSG::TimeTickInformation sample;
    sample.TimeTickMessage().TimeTick(987654321ULL);
    sample.TimeTickMessage().SyncCycle(7U);

    for (int attempt = 0; attempt < 10; ++attempt) {
        typed_publisher.Publish(sample);
        std::unique_lock lock(mutex);
        if (changed.wait_for(lock, 300ms, [&] { return received; })) break;
    }

    require(received, "RTI sample was not received by the second participant.");
    require(received_tick == 987654321ULL, "RTI sample payload was corrupted.");
}

} // namespace

void run_rti_tests(const std::filesystem::path& definitions)
{
    test_public_status_and_errors(definitions);
    test_broken_native_error_output(definitions);
    test_close_requests_all_readers_before_shared_deadline();
    test_multicast_override_uses_parsed_qos(definitions);
    test_rti_close_from_callback(definitions);
    test_rti_close_stops_retained_subscription(definitions);
    test_rti_burst(definitions);
    test_first_response_published_from_handler(definitions);
    test_independent_message_modules(definitions);
    test_two_udp_network_bursts(definitions);
    test_rti_same_client(definitions);
    test_rti(definitions);
}
} // namespace dds_test
