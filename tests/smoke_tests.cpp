#include "test_support.hpp"
#include <future>
#include <unordered_map>
#if defined(DDSCPP_ENABLE_RTI)
#include "dds_cpp/transport/RtiTopic.hpp"
#include <algorithm>
#endif
namespace {

using namespace dds_test;
using namespace std::chrono_literals;

struct RetirementProbe {
    std::promise<void> destroying;
    std::promise<void> release;
    std::shared_future<void> released{release.get_future().share()};
    std::thread::id destructor_thread;
};
void test_broken_debug_output(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.LogLevel() = dds_cpp::DdsLogLevel::Debug;
    auto client = dds_cpp::detail::ClientAccess::Create(options,
        std::make_unique<dds_cpp::InMemoryDdsTransport>());
    int received = 0;
    auto subscription = client.Subscribe<MSG::TimeTickInformation>([&](const auto&) { ++received; });
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    {
        BrokenOutput output(std::clog);
        client.Publish(MSG::TimeTickInformation{});
        publisher.Publish(MSG::TimeTickInformation{});
    }
    require(received == 2 && !client.LastSubscriptionError(), "로그 실패가 송수신을 중단했습니다.");
}
class SlowDestroyTransport final : public dds_cpp::IDdsTransport {
public:
    explicit SlowDestroyTransport(std::shared_ptr<RetirementProbe> probe) : probe_(std::move(probe)) {}
    ~SlowDestroyTransport() override {
        probe_->destructor_thread = std::this_thread::get_id();
        probe_->destroying.set_value();
        probe_->released.wait();
        status_->complete();
    }
    void Dispose() noexcept override {}
    void WaitForSubscribers(const dds_cpp::TopicDefinition&, const std::type_info&, int, std::chrono::milliseconds) override {}
    void WaitForAcknowledgments(const dds_cpp::TopicDefinition&, const std::type_info&, std::chrono::milliseconds) override {}
    void CreatePublisher(const dds_cpp::TopicDefinition&, const std::type_info&) override {}
    void Publish(const dds_cpp::TopicDefinition&, const std::type_info&, const void*) override {}
    std::unique_ptr<dds_cpp::Subscription> Subscribe(const dds_cpp::TopicDefinition&, const std::type_info&,
        dds_cpp::TypedSampleHandler, std::uint64_t) override { return {}; }
private:
    std::shared_ptr<RetirementProbe> probe_;
};

void test_background_final_release(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.ShutdownTimeout() = 20ms;
    for (bool in_flight : {false, true}) {
        auto probe = std::make_shared<RetirementProbe>();
        auto destroying = probe->destroying.get_future();
        auto client = dds_cpp::detail::ClientAccess::Create(options, std::make_unique<SlowDestroyTransport>(probe));
        auto retained = in_flight ? dds_cpp::detail::ClientAccess::RetainTransport(client) : nullptr;
        std::thread::id releaser;
        auto call = std::async(std::launch::async, [&] {
            releaser = std::this_thread::get_id();
            client.Close();
            retained.reset(); // Models the last publish/read operation returning.
        });
        const bool entered = destroying.wait_for(2s) == std::future_status::ready;
        const bool bounded = call.wait_for(300ms) == std::future_status::ready;
        const bool pending = client.ShutdownState() == dds_cpp::DdsShutdownState::Closing;
        probe->release.set_value(); // Always unblock before any assertion unwinds.
        call.get();
        require(entered && bounded && pending, "Native destruction escaped the background owner or Close timeout.");
        require(probe->destructor_thread != releaser, "Native destruction ran on the releasing application thread.");
        require(client.WaitForShutdown(2s), "Background destruction never completed.");
    }

    // Creation failure must not fall back to synchronous close/destruction.
    auto probe = std::make_shared<RetirementProbe>();
    auto destroying = probe->destroying.get_future();
    auto client = dds_cpp::detail::ClientAccess::Create(options, std::make_unique<SlowDestroyTransport>(probe));
    auto close = std::async(std::launch::async, [&] {
        dds_cpp::detail::ClientAccess::FailNextBackgroundStart();
        client.Close();
    });
    const bool bounded = close.wait_for(300ms) == std::future_status::ready;
    const bool entered = destroying.wait_for(2s) == std::future_status::ready;
    probe->release.set_value();
    close.get();
    require(bounded && entered && client.ShutdownState() == dds_cpp::DdsShutdownState::Faulted,
        "Thread-start failure blocked Close or was hidden.");
    bool failed = false;
    try { client.WaitForShutdown(2s); } catch (const dds_cpp::DdsOperationException&) { failed = true; }
    require(failed, "Thread-start failure must be observable by shutdown waiters.");

    auto retained_probe = std::make_shared<RetirementProbe>();
    auto* raw = new SlowDestroyTransport(retained_probe);
    auto retained_client = dds_cpp::detail::ClientAccess::Create(options, std::unique_ptr<dds_cpp::IDdsTransport>(raw));
    auto held = dds_cpp::detail::ClientAccess::RetainTransport(retained_client);
    retained_client.Close();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (held.use_count() > 1 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    require(held.use_count() == 1, "Close worker did not release its reference.");
    auto native_delete = retained_probe->destroying.get_future();
    dds_cpp::detail::ClientAccess::FailNextBackgroundStart();
    held.reset();
    const bool retained_on_failure = native_delete.wait_for(20ms) == std::future_status::timeout;
    const bool faulted = retained_client.ShutdownState() == dds_cpp::DdsShutdownState::Faulted;
    // The production fallback intentionally retains ownership until process exit.
    // Reclaim this test double explicitly, off-thread, to keep the test leak-free.
    retained_probe->release.set_value();
    std::thread recovery([raw] { delete raw; });
    recovery.join();
    require(retained_on_failure && faulted, "Failed finalizer start performed native destruction or hid failure.");
}

template<class Exception, class Action>
void require_throws(Action action, const std::string& message)
{
    try { action(); } catch (const Exception& error) {
        require(error.what() == message, "Unexpected exception message: " + std::string(error.what()));
        return;
    }
    throw std::runtime_error("Expected exception: " + message);
}

void test_type_topic_index(const std::filesystem::path& definitions)
{
    using dds_cpp::DdsConfiguration;
    using dds_cpp::DdsConfigurationException;
    using dds_cpp::Topic;
    const auto check = [](const DdsConfiguration& configuration) {
        require(&configuration.GetTopic(typeid(double)) == &configuration.GetTopic("Unique"),
            "Type lookup did not refer to this configuration's topic storage.");
        require_throws<DdsConfigurationException>([&] { (void)configuration.GetTopic(typeid(int)); },
            "이 타입에 여러 바인딩이 있습니다. 토픽 이름을 지정하세요.");
        require_throws<DdsConfigurationException>([&] { (void)configuration.GetTopic(typeid(char)); },
            "이 타입에 등록된 토픽이 없습니다.");
        require(configuration.GetTopic("First").sample_type == typeid(int), "Named ambiguous binding was lost.");
    };
    DdsConfiguration original({Topic::Named<int>("First"), Topic::Named<double>("Unique"),
        Topic::Named<int>("Second"), Topic::Named<int>("Third")});
    check(original);
    auto copied = original;
    require(&copied.GetTopic(typeid(double)) != &original.GetTopic(typeid(double)), "Index borrowed source storage.");
    original = DdsConfiguration({});
    check(copied);
    auto moved = std::move(copied);
    check(moved);
    DdsConfiguration assigned({Topic::Named<char>("Old")});
    assigned = moved;
    check(assigned);
    DdsConfiguration move_assigned({});
    move_assigned = std::move(assigned);
    check(move_assigned);
    require_throws<DdsConfigurationException>([&] { (void)original.GetTopic(typeid(double)); },
        "이 타입에 등록된 토픽이 없습니다.");
    require_throws<DdsConfigurationException>([] {
        (void)DdsConfiguration({Topic::Named<int>("Duplicate"), Topic::Named<double>("Duplicate")});
    }, "Topic 'Duplicate' 이(가) 코드에 중복 등록되었습니다.");

    auto options = in_memory_options(definitions);
    options.Topics() = std::vector<dds_cpp::TopicDefinition>{
        Topic::Named<MSG::TimeTickInformation>("TickA"), Topic::Named<MSG::TimeTickInformation>("TickB")};
    auto client = test_client(options);
    const std::string ambiguous = "이 타입에 여러 바인딩이 있습니다. 토픽 이름을 지정하세요.";
    require_throws<DdsConfigurationException>([&] { client.Publish(MSG::TimeTickInformation{}); }, ambiguous);
    require_throws<DdsConfigurationException>([&] { (void)client.CreatePublisher<MSG::TimeTickInformation>(); }, ambiguous);
    require_throws<DdsConfigurationException>([&] {
        (void)client.Subscribe<MSG::TimeTickInformation>([](const auto&) {});
    }, ambiguous);
    int received = 0;
    auto reader = client.Subscribe<MSG::TimeTickInformation>([&](const auto&) { ++received; }, "TickB");
    client.Publish(MSG::TimeTickInformation{}, "TickA");
    client.CreatePublisher<MSG::TimeTickInformation>("TickB").Publish(MSG::TimeTickInformation{});
    require(received == 1, "Named lookup did not disambiguate type bindings.");
    require_throws<dds_cpp::DdsOperationException>([&] { client.Publish(42, "TickA"); },
        "토픽의 샘플 타입이 일치하지 않습니다.");
    auto moved_client = std::move(client);
    moved_client.Publish(MSG::TimeTickInformation{}, "TickB");
    require(received == 2, "Moving the client invalidated topic lookup.");
}

void test_subscription_error_identifiers(const std::filesystem::path& definitions)
{
    auto client = test_client(in_memory_options(definitions));
    std::vector<dds_cpp::DdsSubscriptionError> errors;
    client.SetSubscriptionErrorHandler([&](const auto& error) {
        errors.push_back(error);
        throw std::runtime_error("Observer failure must not stop delivery.");
    });
    auto first = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("first"); });
    auto second = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw 42; });
    client.Publish(MSG::TimeTickInformation{});
    client.Publish(MSG::TimeTickInformation{});
    require(errors.size() == 4, "Throwing handlers or observers stopped subsequent delivery.");
    const auto first_id = errors[0].SubscriptionId;
    const auto second_id = errors[1].SubscriptionId;
    require(first_id != 0 && second_id != 0 && first_id != second_id, "Same-topic subscriptions shared an identifier.");
    require(errors[2].SubscriptionId == first_id && errors[3].SubscriptionId == second_id,
        "Subscription identifiers changed between handler failures.");
    for (const auto& error : errors)
        require(!error.IsFatal && error.TopicName == "TimeTickInformation" && error.Exception,
            "Handler error metadata changed.");
    require_throws<std::runtime_error>([&] { std::rethrow_exception(errors[0].Exception); }, "first");
    bool nonstandard_preserved = false;
    try { std::rethrow_exception(errors[1].Exception); } catch (int value) { nonstandard_preserved = value == 42; }
    require(nonstandard_preserved, "Non-standard handler exception was lost.");
    require(client.LastSubscriptionError()->SubscriptionId == second_id, "Last error lost its subscription identifier.");
    first.reset();
    auto replacement = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("replacement"); });
    auto moved = std::move(client);
    moved.Publish(MSG::TimeTickInformation{});
    require(errors.size() == 6 && errors[4].SubscriptionId == second_id,
        "Move or unsubscribe changed the surviving subscription's identifier.");
    const auto replacement_id = errors[5].SubscriptionId;
    require(replacement_id != 0 && replacement_id != first_id && replacement_id != second_id,
        "Replacement subscription reused an identifier.");

    auto other = test_client(in_memory_options(definitions));
    auto other_reader = other.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("other"); });
    other.Publish(MSG::TimeTickInformation{});
    const auto other_id = other.LastSubscriptionError()->SubscriptionId;
    require(other_id != 0 && other_id != first_id && other_id != second_id && other_id != replacement_id,
        "Different clients reused subscription identifiers.");
}

// Inject receive failures before Subscribe returns, without relying on native
// reader failures that cannot be triggered deterministically on every RTI build.
class FailingReaderTransport final : public dds_cpp::IDdsTransport {
public:
    ~FailingReaderTransport() override { status_->complete(); }
    void Dispose() noexcept override {}
    void WaitForSubscribers(const dds_cpp::TopicDefinition&, const std::type_info&, int, std::chrono::milliseconds) override {}
    void WaitForAcknowledgments(const dds_cpp::TopicDefinition&, const std::type_info&, std::chrono::milliseconds) override {}
    void CreatePublisher(const dds_cpp::TopicDefinition&, const std::type_info&) override {}
    void Publish(const dds_cpp::TopicDefinition&, const std::type_info&, const void*) override {}
    std::unique_ptr<dds_cpp::Subscription> Subscribe(const dds_cpp::TopicDefinition& topic,
        const std::type_info&, dds_cpp::TypedSampleHandler handler, std::uint64_t subscription_id) override
    {
        MSG::TimeTickInformation sample;
        handler(&sample);
        status_->report({topic.name, std::make_exception_ptr(std::runtime_error("reader failure")), true, subscription_id});
        return std::make_unique<dds_cpp::Subscription>();
    }
};

void test_immediate_fatal_subscription_identifiers(const std::filesystem::path& definitions)
{
    auto client = dds_cpp::detail::ClientAccess::Create(in_memory_options(definitions), std::make_unique<FailingReaderTransport>());
    std::vector<dds_cpp::DdsSubscriptionError> errors;
    std::mutex errors_mutex;
    client.SetSubscriptionErrorHandler([&](const auto& error) {
        std::lock_guard lock(errors_mutex);
        errors.push_back(error);
    });
    auto first = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("handler failure"); });
    auto second = client.Subscribe<MSG::TimeTickInformation>([](const auto&) { throw std::runtime_error("handler failure"); });
    require(errors.size() == 4, "Immediate handler/fatal errors were lost.");
    for (std::size_t i = 0; i < errors.size(); i += 2) {
        require(errors[i].SubscriptionId != 0 && errors[i].SubscriptionId == errors[i + 1].SubscriptionId,
            "Handler and fatal errors did not share the subscription identifier.");
        require(!errors[i].IsFatal && errors[i + 1].IsFatal && errors[i + 1].TopicName == "TimeTickInformation",
            "Immediate fatal error metadata changed.");
        require_throws<std::runtime_error>([&] { std::rethrow_exception(errors[i + 1].Exception); }, "reader failure");
    }
    require(errors[0].SubscriptionId != errors[2].SubscriptionId, "Fatal errors on the same topic shared an identifier.");
    const auto last = client.LastSubscriptionError();
    require(last && last->IsFatal && last->SubscriptionId == errors.back().SubscriptionId, "Last fatal error lost its identifier.");

    const auto first_id = errors.front().SubscriptionId;
    const auto second_id = errors.back().SubscriptionId;
    errors.clear();
    std::vector<std::future<void>> workers;
    for (int worker = 0; worker < 8; ++worker)
        workers.push_back(std::async(std::launch::async, [&] {
            for (int i = 0; i < 16; ++i) {
                auto reader = client.Subscribe<MSG::TimeTickInformation>([](const auto&) {
                    throw std::runtime_error("concurrent handler failure");
                });
            }
        }));
    for (auto& worker : workers) worker.get();
    std::unordered_map<std::uint64_t, std::pair<int, int>> counts;
    for (const auto& error : errors) {
        require(error.SubscriptionId != 0 && error.SubscriptionId != first_id && error.SubscriptionId != second_id,
            "Concurrent allocation reused an identifier.");
        auto& count = counts[error.SubscriptionId];
        ++(error.IsFatal ? count.second : count.first);
    }
    require(errors.size() == 256 && counts.size() == 128, "Concurrent subscriptions did not receive unique identifiers.");
    for (const auto& [id, count] : counts)
        require(count.first == 1 && count.second == 1, "Concurrent handler/fatal identifiers did not match.");
}

#if defined(DDSCPP_ENABLE_RTI)
void test_writer_key()
{
    using namespace dds_cpp::detail;
    std::unordered_map<WriterKey, int, WriterKeyHash, WriterKeyEqual> keys;
    std::string name = "A_long_topic_name_for_writer_lookup";
    keys.emplace(WriterKey{name, typeid(int)}, 1);
    keys.emplace(WriterKey{name, typeid(double)}, 2);
    name[0] = 'B';
    const std::string original = "A_long_topic_name_for_writer_lookup";
    require(keys.at(WriterKey{original, typeid(int)}) == 1, "Stored writer key borrowed temporary name.");
    require(keys.find(WriterKeyView{original, typeid(double)})->second == 2, "Writer types collided.");
    require(keys.find(WriterKeyView{name, typeid(int)}) == keys.end(), "Writer names collided.");
    if (std::getenv("DDS_BENCHMARK") == nullptr) return;
    std::unordered_map<std::string, int> old;
    old.emplace(original + "\n" + typeid(int).name(), 1);
    std::size_t sink = 0;
    auto measure = [&](const char* label, auto lookup) {
        std::vector<double> times;
        for (int round = 0; round < 8; ++round) {
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 1000000; ++i) sink += lookup();
            if (round) times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count());
        }
        std::sort(times.begin(), times.end());
        std::cout << "BENCH " << label << " 1000000 lookups median_ms=" << times[3] << '\n';
    };
    measure("cpp concatenated-key", [&] { return old.find(original + "\n" + typeid(int).name())->second; });
    measure("cpp structured-key", [&] { return keys.find(WriterKeyView{original, typeid(int)})->second; });
    require(sink == 16000000, "Lookup benchmark was not executed.");
}
#endif

void test_faulted_client_rejects_all_operations(const std::filesystem::path& definitions)
{
    auto transport = std::make_unique<dds_cpp::InMemoryDdsTransport>();
    auto status = transport->status();
    auto client = dds_cpp::detail::ClientAccess::Create(in_memory_options(definitions), std::move(transport));
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    status->fault(std::make_exception_ptr(std::runtime_error("Injected cleanup failure")));
    require(!client.IsOpen(), "Faulted client reported open.");
    auto rejects = [](auto action) {
        try { action(); } catch (const dds_cpp::DdsOperationException&) { return; }
        throw std::runtime_error("Faulted client accepted operation.");
    };
    rejects([&] { (void)client.CreatePublisher<MSG::TimeTickInformation>(); });
    rejects([&] { (void)client.Subscribe<MSG::TimeTickInformation>([](const auto&) {}); });
    rejects([&] { publisher.Publish(MSG::TimeTickInformation{}); });
    rejects([&] { publisher.WaitForSubscribers(1, 0ms); });
    rejects([&] { publisher.WaitForAcknowledgments(0ms); });
    client.Close();
    require(client.ShutdownState() == dds_cpp::DdsShutdownState::Faulted && client.ShutdownError(), "Close erased failure.");
}

void test_configuration(const std::filesystem::path& definitions)
{
    const auto configuration = dds_cpp::DdsConfigurationReader::Load(in_memory_options(definitions));
    require(configuration.Topics().size() == 16, "Expected 16 configured topics.");
    require(configuration.GetTopic("TimeTickInformation").Name() == "TimeTickInformation", "TimeTickInformation is missing.");

    auto options = in_memory_options(definitions);
    options.DomainId() = 17;
    options.ParticipantName() = "A & B";
    auto client = test_client(options);
    require(client.Options().DomainId() == 17, "Code domain was not retained.");
    require(client.Options().ParticipantName() == "A & B", "Code participant name was not retained.");
}

void test_qos_xml_parser(const std::filesystem::path&)
{
    const auto file = std::filesystem::temp_directory_path() /
        ("dds-qos-parser-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".xml");
    struct RemoveFile {
        std::filesystem::path path;
        ~RemoveFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } remove{file};
    {
        std::ofstream output(file);
        output << "<dds><qos_library name='AmbassadorProfiles'>"
                  "<qos_profile name='ReliableReal&#116;ime'/></qos_library></dds>";
    }
    require(dds_cpp::DdsConfigurationReader::Load(std::nullopt, file.string(), {dds_cpp::DDSSimMessages()}).Topics().size() == 16,
        "QoS XML character references were not decoded.");
    {
        std::ofstream output(file);
        output << "<dds><qos_library name='AmbassadorProfiles'>"
                  "<qos_profile name='ReliableRealtime'><broken></qos_profile></qos_library></dds>";
    }
    bool rejected = false;
    try { (void)dds_cpp::DdsConfigurationReader::Load(std::nullopt, file.string()); }
    catch (const dds_cpp::DdsConfigurationException&) { rejected = true; }
    require(rejected, "Malformed nested QoS XML was accepted.");
}

void test_in_memory(const std::filesystem::path& definitions)
{
    std::unique_ptr<dds_cpp::Subscription> subscription;
    {
        auto client = test_client(in_memory_options(definitions));
        bool received = false;
        subscription = client.Subscribe<MSG::TimeTickInformation>([&](const auto& tick) {
            received = tick.TimeTickMessage().TimeTick() == 42;
        });
        MSG::TimeTickInformation sample;
        sample.TimeTickMessage().TimeTick(42);
        client.Publish(sample);
        require(received, "InMemory delivery failed.");
    }
    subscription.reset();
}

void test_in_memory_rejects_conflicting_topic_type(const std::filesystem::path& definitions)
{
    auto client = test_client(in_memory_options(definitions));
    bool threw = false;
    try { client.Publish(42); }
    catch (const dds_cpp::DdsConfigurationException&) { threw = true; }
    require(threw, "Non-message type accepted.");
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
    options.QosProfilesXmlPath() = qos.string();
    bool threw = false;
    try {
        (void)dds_cpp::DdsConfigurationReader::Load(options);
    } catch (const dds_cpp::DdsConfigurationException& exception) {
        threw = std::string(exception.what()).find("AmbassadorProfiles") != std::string::npos;
    }
    std::filesystem::remove_all(directory);
    require(threw, "A QoS profile from the wrong library was accepted.");
}

void test_configuration_root_is_validated(const std::filesystem::path& definitions)
{
    const auto directory = std::filesystem::temp_directory_path() /
        ("ddscpp-qos-only-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto qos = directory / "qos_profiles.xml";
    std::filesystem::copy_file(definitions / "qos_profiles.xml", qos);
    require(dds_cpp::DdsConfigurationReader::Load(std::nullopt, qos.string(), {dds_cpp::DDSSimMessages()}).Topics().size() == 16,
        "Runtime configuration should not require DDSSim.xml.");
    { std::ofstream output(qos); output << "<wrong/>"; }
    bool threw = false;
    try { (void)dds_cpp::DdsConfigurationReader::Load(std::nullopt, qos.string()); }
    catch (const dds_cpp::DdsConfigurationException&) { threw = true; }
    std::filesystem::remove_all(directory);
    require(threw, "Wrong QoS root accepted.");
}

void test_close_is_thread_safe(const std::filesystem::path& definitions)
{
    auto client = test_client(in_memory_options(definitions));
    std::atomic_bool stop{false};
    std::thread worker([&] {
        while (!stop.load()) {
            (void)client.IsOpen();
            try {
                client.Publish(MSG::TimeTickInformation{});
            } catch (const dds_cpp::DdsOperationException&) {
            }
        }
    });
    client.Close();
    stop = true;
    worker.join();
    require(!client.IsOpen(), "Concurrent close left the client open.");
}

void test_nuget_api_parity(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    auto client = test_client(options);

    bool received = false;
    auto subscription = client.Subscribe<MSG::TimeTickInformation>(
        [&](const MSG::TimeTickInformation& sample) {
            received = sample.TimeTickMessage().TimeTick() == 1234ULL;
        });

    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    require(publisher.Topic().name == "TimeTickInformation", "Publisher topic does not match NuGet API.");
    require(
        publisher.Topic().QualifiedQosProfile() == "AmbassadorProfiles::ReliableRealtime",
        "QualifiedQosProfile does not match NuGet API.");

    MSG::TimeTickInformation sample;
    sample.TimeTickMessage().TimeTick(1234ULL);
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
    auto client = test_client(options);
    auto publisher = client.CreatePublisher<MSG::TimeTickInformation>();
    client.Close();
    require(!client.IsOpen(), "Disposed C++ client still reports open.");

    bool client_threw = false;
    try {
        client.Publish(MSG::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        client_threw = std::string(exception.what()).find("닫혔습니다") != std::string::npos;
    }
    require(client_threw, "Disposed C++ client did not reject publish.");

    bool publisher_threw = false;
    try {
        publisher.Publish(MSG::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        publisher_threw = std::string(exception.what()).find("닫혔습니다") != std::string::npos;
    }
    require(publisher_threw, "Publisher outlived its closed client.");

    auto reconnected = test_client(options);
    bool received = false;
    auto subscription = reconnected.Subscribe<MSG::TimeTickInformation>(
        [&](const MSG::TimeTickInformation&) { received = true; });
    reconnected.Publish(MSG::TimeTickInformation{});
    require(received, "Reconnected C++ client did not deliver a sample.");
}

void test_direction_policy_matches_nuget(const std::filesystem::path& definitions)
{
    auto options = in_memory_options(definitions);
    options.Topics() = std::vector<dds_cpp::TopicDefinition>{
        {"TimeTickInformation", "ReliableRealtime", dds_cpp::TopicDirection::Subscribe}};
    auto client = test_client(options);

    bool threw = false;
    try {
        client.Publish(MSG::TimeTickInformation{});
    } catch (const dds_cpp::DdsOperationException& exception) {
        threw = std::string(exception.what()).find("발행할 수 없습니다") != std::string::npos;
    }
    require(threw, "Subscribe-only topic did not reject publish like the NuGet client.");
}

void test_code_registrations(const std::filesystem::path& definitions)
{
    using dds_cpp::TopicDefinition;
    using dds_cpp::TopicDirection;
    auto options = in_memory_options(definitions);
    options.Topics() = std::vector<TopicDefinition>{};
    auto empty_client = test_client(options);
    require(empty_client.Configuration().Topics().empty(), "Explicit empty registrations enabled messages.");
    bool empty_threw = false;
    try { empty_client.Publish(MSG::TimeTickInformation{}); }
    catch (const dds_cpp::DdsConfigurationException&) { empty_threw = true; }
    require(empty_threw, "An empty registration list allowed publishing.");

    options.Topics() = std::vector<TopicDefinition>{
        dds_cpp::Topic::Publish<MSG::TimeTickInformation>()};
    auto client = test_client(options);
    options.Topics()->clear();
    require(client.Configuration().Topics().size() == 1, "Client did not snapshot code registrations.");
    client.Publish(MSG::TimeTickInformation{});
    bool direction_threw = false;
    try { auto subscription = client.Subscribe<MSG::TimeTickInformation>([](const auto&) {}); }
    catch (const dds_cpp::DdsOperationException&) { direction_threw = true; }
    require(direction_threw, "Publish-only registration allowed subscription.");
    const dds_cpp::TopicDefinition* absent = nullptr;
    require(!client.Configuration().TryGetTopic("WeaponFire", absent), "Unregistered message was enabled.");

    const std::vector<std::vector<TopicDefinition>> invalid{
        {{"UnknownMessage", "ReliableRealtime", TopicDirection::Both}},
        {{"", "ReliableRealtime", TopicDirection::Both}},
        {{"TimeTickInformation", "", TopicDirection::Both}},
        {{"TimeTickInformation", "MissingProfile", TopicDirection::Both}},
        {{"TimeTickInformation", "ReliableRealtime", static_cast<TopicDirection>(99)}},
        {{"TimeTickInformation", "ReliableRealtime", TopicDirection::Both},
         {"TimeTickInformation", "ReliableRealtime", TopicDirection::Subscribe}}
    };
    for (const auto& registrations : invalid) {
        options.Topics() = registrations;
        bool threw = false;
        try { (void)dds_cpp::DdsConfigurationReader::Load(options); }
        catch (const dds_cpp::DdsConfigurationException&) { threw = true; }
        require(threw, "Invalid code registration was accepted.");
    }
}

void test_path_precedence_matches_nuget(const std::filesystem::path& definitions)
{
    const auto* current_qos = std::getenv("DDS_QOS_PROFILES_XML_PATH");
    const auto* current_sim = std::getenv("DDS_DDSSIM_XML_PATH");
    const std::string original_qos = current_qos == nullptr ? "" : current_qos;
    const std::string original_sim = current_sim == nullptr ? "" : current_sim;

    set_environment("DDS_QOS_PROFILES_XML_PATH", "missing-qos.xml");
    set_environment("DDS_DDSSIM_XML_PATH", "missing-sim.xml");
    const auto explicit_configuration = dds_cpp::DdsConfigurationReader::Load(in_memory_options(definitions));
    require(explicit_configuration.Topics().size() == 16, "Explicit Options did not override environment paths.");

    dds_cpp::DdsClientOptions direct_options;
    direct_options.MessageModules() = {dds_cpp::DDSSimMessages()};
    const auto direct_configuration = dds_cpp::DdsConfigurationReader::Load(direct_options);
    require(direct_configuration.Topics().size() == 16, "Direct Options unexpectedly read environment paths.");

    set_environment("DDS_QOS_PROFILES_XML_PATH", current_qos == nullptr ? nullptr : original_qos.c_str());
    set_environment("DDS_DDSSIM_XML_PATH", current_sim == nullptr ? nullptr : original_sim.c_str());
}



} // namespace

int main()
{
    try {
        const auto definitions = find_repo_root() / "definitions";
        {
            const auto relative = std::filesystem::path(u8"한글경로") / std::filesystem::path(u8"품질설정.xml");
            const auto encoded = relative.u8string();
            const std::string input(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            dds_cpp::DdsNetwork network("경로검증", dds_cpp::DDSSimMessages(), 0,
                input, "AmbassadorProfiles::DefaultParticipant", "AmbassadorProfiles::ReliableRealtime");
            require(std::filesystem::u8path(network.CreateClientOptions().QosProfilesXmlPath()) ==
                std::filesystem::absolute(relative), "망 설정의 UTF-8 경로가 손상되었습니다.");
        }
        test_configuration(definitions);
        test_broken_debug_output(definitions);
        test_background_final_release(definitions);
        test_type_topic_index(definitions);
        test_subscription_error_identifiers(definitions);
        test_immediate_fatal_subscription_identifiers(definitions);
        test_faulted_client_rejects_all_operations(definitions);
        test_qos_xml_parser(definitions);
        test_in_memory(definitions);
        test_in_memory_rejects_conflicting_topic_type(definitions);
        test_qos_library_name_is_validated(definitions);
        test_configuration_root_is_validated(definitions);
        test_close_is_thread_safe(definitions);
        test_nuget_api_parity(definitions);
        test_close_and_reconnect(definitions);
        test_direction_policy_matches_nuget(definitions);
        test_code_registrations(definitions);
        test_path_precedence_matches_nuget(definitions);
#if defined(DDSCPP_ENABLE_RTI)
        test_writer_key();
        dds_test::run_rti_tests(definitions);
#endif
        std::cout << "DDSCPP configuration, code registration, in-memory, and lifetime tests passed.\n";
#if defined(DDSCPP_ENABLE_RTI)
        std::cout << "DDSCPP RTI communication test passed.\n";
#endif
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << '\n';
        return 1;
    }
}
