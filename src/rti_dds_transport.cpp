#include "dds_cpp/rti_dds_transport.hpp"

#include <iostream>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <sstream>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dds_cpp/dds_exception.hpp"
#include "dds_path_utils.hpp"

#if defined(DDSCPP_ENABLE_RTI)
#include "DDSSim.hpp"
#include <dds/dds.hpp>
#include <rti/core/policy/CorePolicy.hpp>
#endif

namespace dds_cpp {

#if defined(DDSCPP_ENABLE_RTI)

namespace {

std::string trim(std::string value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string environment_value(const char* name)
{
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : trim(value);
}

void validate_multicast_address(const std::string& value)
{
    std::array<int, 4> octets{};
    char separator1 = 0, separator2 = 0, separator3 = 0;
    std::istringstream input(value);
    if (!(input >> octets[0] >> separator1 >> octets[1] >> separator2 >> octets[2] >> separator3 >> octets[3]) ||
        separator1 != '.' || separator2 != '.' || separator3 != '.' ||
        octets[0] < 224 || octets[0] > 239 ||
        octets[1] < 0 || octets[1] > 255 || octets[2] < 0 || octets[2] > 255 ||
        octets[3] < 0 || octets[3] > 255) {
        throw DdsConfigurationException(
            "'" + value + "' must be an IPv4 multicast address in 224.0.0.0/4.");
    }
    input >> std::ws;
    if (!input.eof()) {
        throw DdsConfigurationException(
            "'" + value + "' must be an IPv4 multicast address in 224.0.0.0/4.");
    }
}

class EffectiveQosFile final {
public:
    explicit EffectiveQosFile(const std::string& configured_path)
        : path_(detail::resolve_required_file(
              configured_path,
              "definitions/qos_profiles.xml",
              "qos_profiles_xml_path"))
    {
        const auto general = environment_value("DDS_MULTICAST_ADDRESS");
        const auto receive = environment_value("DDS_MULTICAST_RECEIVE_ADDRESS");
        auto general_comparison = general;
        auto receive_comparison = receive;
        std::transform(general_comparison.begin(), general_comparison.end(), general_comparison.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(receive_comparison.begin(), receive_comparison.end(), receive_comparison.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (!general.empty() && !receive.empty() && general_comparison != receive_comparison) {
            throw DdsConfigurationException(
                "DDS_MULTICAST_ADDRESS must match DDS_MULTICAST_RECEIVE_ADDRESS when both are set.");
        }
        const auto selected = !receive.empty() ? receive : general;
        if (selected.empty()) return;
        validate_multicast_address(selected);

        std::ifstream input(path_, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        auto xml = buffer.str();
        const std::string open = "<receive_address>";
        const std::string close = "</receive_address>";
        std::size_t cursor = 0;
        std::size_t replacements = 0;
        while ((cursor = xml.find(open, cursor)) != std::string::npos) {
            const auto value_begin = cursor + open.size();
            const auto value_end = xml.find(close, value_begin);
            if (value_end == std::string::npos) break;
            xml.replace(value_begin, value_end - value_begin, selected);
            cursor = value_begin + selected.size() + close.size();
            ++replacements;
        }
        if (replacements == 0) {
            throw DdsConfigurationException(
                "QoS profile '" + path_ + "' does not contain a multicast receive_address element.");
        }

        const auto unique = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        temporary_path_ = std::filesystem::temp_directory_path() /
            ("ddsclient-qos-" + std::to_string(unique) + ".xml");
        std::ofstream output(temporary_path_, std::ios::binary | std::ios::trunc);
        output << xml;
        if (!output) throw DdsConfigurationException("Failed to create temporary DDS QoS profile.");
        path_ = temporary_path_.string();
    }

    ~EffectiveQosFile()
    {
        if (!temporary_path_.empty()) {
            std::error_code ignored;
            std::filesystem::remove(temporary_path_, ignored);
        }
    }

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
    std::filesystem::path temporary_path_;
};

std::string writer_key(const TopicDefinition& topic, const std::type_info& type)
{
    return topic.name + "\n" + type.name();
}

std::string qualified_profile(const TopicDefinition& topic)
{
    return topic.qualified_qos_profile();
}

class WriterBase {
public:
    virtual ~WriterBase() = default;
    virtual void write(const void* sample) = 0;
};

template <class T>
class TypedWriter final : public WriterBase {
public:
    TypedWriter(
        const dds::domain::DomainParticipant& participant,
        const dds::pub::Publisher& publisher,
        dds::core::QosProvider& qos_provider,
        const TopicDefinition& definition)
        : topic_(participant, definition.name)
        , writer_(publisher, topic_, qos_provider.datawriter_qos(qualified_profile(definition)))
    {
    }

    void write(const void* sample) override
    {
        writer_.write(*static_cast<const T*>(sample));
    }

private:
    dds::topic::Topic<T> topic_;
    dds::pub::DataWriter<T> writer_;
};

template <class T> class TypedRtiSubscriptionState;

template <class T>
class TypedReaderListener final : public dds::sub::NoOpDataReaderListener<T> {
public:
    void set_owner(std::weak_ptr<TypedRtiSubscriptionState<T>> owner)
    {
        owner_ = std::move(owner);
    }

    void on_data_available(dds::sub::DataReader<T>& reader) override
    {
        if (const auto owner = owner_.lock()) {
            owner->on_data_available(reader);
        }
    }

private:
    std::weak_ptr<TypedRtiSubscriptionState<T>> owner_;
};

template <class T>
class TypedRtiSubscriptionState final
    : public std::enable_shared_from_this<TypedRtiSubscriptionState<T>> {
public:
    static std::shared_ptr<TypedRtiSubscriptionState> create(
        std::shared_ptr<void> transport_owner,
        const dds::domain::DomainParticipant& participant,
        const dds::sub::Subscriber& subscriber,
        dds::core::QosProvider& qos_provider,
        const TopicDefinition& definition,
        TypedSampleHandler handler,
        DdsLogLevel log_level)
    {
        auto state = std::shared_ptr<TypedRtiSubscriptionState>(new TypedRtiSubscriptionState(
            std::move(transport_owner), participant, definition, std::move(handler), log_level));
        state->listener_.set_owner(state);
        state->reader_.emplace(
            subscriber,
            state->topic_,
            qos_provider.datareader_qos(qualified_profile(definition)),
            &state->listener_,
            dds::core::status::StatusMask::data_available());
        return state;
    }

    void on_data_available(dds::sub::DataReader<T>& reader)
    {
        std::lock_guard callback_lock(callback_mutex_);
        if (closing_) return;

        struct CallbackScope final {
            explicit CallbackScope(const TypedRtiSubscriptionState* state)
                : previous(active_callback_)
            {
                active_callback_ = state;
            }
            ~CallbackScope() { active_callback_ = previous; }
            const TypedRtiSubscriptionState* previous;
        } callback_scope(this);

        try {
            auto samples = reader.take();
            for (const auto& sample : samples) {
                if (!sample.info().valid()) continue;
                try {
                    handler_(&sample.data());
                } catch (const std::exception& ex) {
                    if (log_level_ != DdsLogLevel::None) {
                        std::cerr << "DDS subscriber handler failed: " << ex.what() << '\n';
                    }
                } catch (...) {
                    if (log_level_ != DdsLogLevel::None) {
                        std::cerr << "DDS subscriber handler failed with an unknown exception.\n";
                    }
                }
                if (closing_) return;
            }
        } catch (const std::exception& ex) {
            if (log_level_ != DdsLogLevel::None) {
                std::cerr << "DDS reader failed: " << ex.what() << '\n';
            }
        }
    }

    void request_close() noexcept
    {
        if (closing_.exchange(true)) return;
        if (active_callback_ != this) {
            close();
            return;
        }

        auto self = this->shared_from_this();
        failed_cleanup_retainer_ = self;
        try {
            std::thread([self] { self->close(); }).detach();
            failed_cleanup_retainer_.reset();
        } catch (...) {
            // Resource exhaustion must not terminate a destructor running in a DDS callback.
            // Retaining the state is safer than destroying the reader/listener while RTI is using it.
            // The self-reference is intentionally retained as a last-resort leak.
        }
    }

private:
    TypedRtiSubscriptionState(
        std::shared_ptr<void> owner,
        const dds::domain::DomainParticipant& participant,
        const TopicDefinition& definition,
        TypedSampleHandler handler,
        DdsLogLevel log_level)
        : owner_(std::move(owner))
        , topic_(participant, definition.name)
        , handler_(std::move(handler))
        , log_level_(log_level)
    {
    }

    void close() noexcept
    {
        std::lock_guard callback_lock(callback_mutex_);
        try {
            if (reader_) {
                reader_->listener(nullptr, dds::core::status::StatusMask::none());
                reader_->close();
            }
        } catch (...) {
        }
    }

private:
    std::shared_ptr<void> owner_;
    dds::topic::Topic<T> topic_;
    TypedReaderListener<T> listener_;
    TypedSampleHandler handler_;
    DdsLogLevel log_level_;
    std::optional<dds::sub::DataReader<T>> reader_;
    std::mutex callback_mutex_;
    std::atomic_bool closing_{false};
    std::shared_ptr<TypedRtiSubscriptionState> failed_cleanup_retainer_;
    inline static thread_local const TypedRtiSubscriptionState* active_callback_ = nullptr;
};

template <class T>
class TypedRtiSubscription final : public Subscription {
public:
    TypedRtiSubscription(
        std::shared_ptr<void> owner,
        const dds::domain::DomainParticipant& participant,
        const dds::sub::Subscriber& subscriber,
        dds::core::QosProvider& qos_provider,
        const TopicDefinition& definition,
        TypedSampleHandler handler,
        DdsLogLevel log_level)
        : state_(TypedRtiSubscriptionState<T>::create(
              std::move(owner), participant, subscriber, qos_provider, definition,
              std::move(handler), log_level))
    {
    }

    ~TypedRtiSubscription() override
    {
        state_->request_close();
    }

private:
    std::shared_ptr<TypedRtiSubscriptionState<T>> state_;
};

} // namespace

class RtiDdsTransport::Impl final : public std::enable_shared_from_this<Impl> {
public:
    explicit Impl(DdsClientOptions options)
        : options_(std::move(options))
        , effective_qos_file_(options_.qos_profiles_xml_path)
        , qos_provider_(effective_qos_file_.path())
        , participant_(create_participant(options_, qos_provider_))
        , publisher_(participant_)
        , subscriber_(participant_)
    {
    }

    void create_writer(const TopicDefinition& topic, const std::type_info& type)
    {
        std::lock_guard lock(mutex_);
        const auto key = writer_key(topic, type);
        if (writers_.contains(key)) return;
        writers_.emplace(key, make_writer(topic, type));
    }

    void publish(const TopicDefinition& topic, const std::type_info& type, const void* sample)
    {
        if (sample == nullptr) throw DdsOperationException("RTI publish requires a sample.");
        WriterBase* writer = nullptr;
        {
            std::lock_guard lock(mutex_);
            const auto key = writer_key(topic, type);
            auto it = writers_.find(key);
            if (it == writers_.end()) {
                it = writers_.emplace(key, make_writer(topic, type)).first;
            }
            writer = it->second.get();
        }
        writer->write(sample);
    }

    std::unique_ptr<Subscription> subscribe(
        const TopicDefinition& topic,
        const std::type_info& type,
        TypedSampleHandler handler)
    {
        if (!handler) throw DdsOperationException("RTI subscription requires a handler.");

#define DDSCPP_REGISTER_RTI_TYPE(cpp_type, enum_name, dds_name) \
        if (type == typeid(cpp_type)) { \
            if (topic.name != dds_name) { \
                throw DdsOperationException("Topic '" + topic.name + "' requires generated type '" + topic.name + "'."); \
            } \
            return std::make_unique<TypedRtiSubscription<cpp_type>>( \
                shared_from_this(), participant_, subscriber_, qos_provider_, topic, std::move(handler), options_.log_level); \
        }
#include "dds_cpp_rti_type_registry.inc"
#undef DDSCPP_REGISTER_RTI_TYPE

        throw DdsOperationException(
            "C++ type '" + std::string(type.name()) + "' is not a generated MSG type from topics.xml.");
    }

private:
    static dds::domain::DomainParticipant create_participant(
        const DdsClientOptions& options,
        dds::core::QosProvider& qos_provider)
    {
        auto qos = qos_provider.participant_qos();
        if (!options.participant_name.empty()) {
            qos->participant_name.name(options.participant_name);
        }
        if (!options.initial_peers.empty()) {
            dds::core::StringSeq peers(options.initial_peers.begin(), options.initial_peers.end());
            qos->discovery.initial_peers(peers);
        }
        return dds::domain::DomainParticipant(options.domain_id, qos);
    }

    std::unique_ptr<WriterBase> make_writer(const TopicDefinition& topic, const std::type_info& type)
    {
#define DDSCPP_REGISTER_RTI_TYPE(cpp_type, enum_name, dds_name) \
        if (type == typeid(cpp_type)) { \
            if (topic.name != dds_name) { \
                throw DdsOperationException("Topic '" + topic.name + "' requires generated type '" + topic.name + "'."); \
            } \
            return std::make_unique<TypedWriter<cpp_type>>(participant_, publisher_, qos_provider_, topic); \
        }
#include "dds_cpp_rti_type_registry.inc"
#undef DDSCPP_REGISTER_RTI_TYPE

        throw DdsOperationException(
            "C++ type '" + std::string(type.name()) + "' is not a generated MSG type from topics.xml.");
    }

    DdsClientOptions options_;
    EffectiveQosFile effective_qos_file_;
    dds::core::QosProvider qos_provider_;
    dds::domain::DomainParticipant participant_;
    dds::pub::Publisher publisher_;
    dds::sub::Subscriber subscriber_;
    std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<WriterBase>> writers_;
};

#else

class RtiDdsTransport::Impl final {
};

#endif

RtiDdsTransport::RtiDdsTransport(DdsClientOptions options)
{
#if defined(DDSCPP_ENABLE_RTI)
    try {
        impl_ = std::make_shared<Impl>(std::move(options));
    } catch (const DdsConfigurationException&) {
        throw;
    } catch (const std::exception& ex) {
        throw DdsOperationException("Failed to initialize RTI Connext DDS: " + std::string(ex.what()));
    }
#else
    (void)options;
    throw DdsOperationException("RtiDdsTransport was built without RTI Connext DDS support.");
#endif
}

RtiDdsTransport::~RtiDdsTransport() = default;

void RtiDdsTransport::create_publisher(const TopicDefinition&, std::string)
{
    throw DdsOperationException("RTI raw-byte publishers are unsupported; use a generated MSG C++ type.");
}

std::unique_ptr<Subscription> RtiDdsTransport::subscribe(const TopicDefinition&, std::string, SampleHandler)
{
    throw DdsOperationException("RTI raw-byte subscriptions are unsupported; use a generated MSG C++ type.");
}

void RtiDdsTransport::publish(const TopicDefinition&, std::string, ByteSpan)
{
    throw DdsOperationException("RTI raw-byte publish is unsupported; use a generated MSG C++ type.");
}

void RtiDdsTransport::create_typed_publisher(const TopicDefinition& topic, const std::type_info& sample_type)
{
#if defined(DDSCPP_ENABLE_RTI)
    impl_->create_writer(topic, sample_type);
#else
    (void)topic;
    (void)sample_type;
    throw DdsOperationException("RTI Connext DDS support is disabled.");
#endif
}

std::unique_ptr<Subscription> RtiDdsTransport::subscribe_typed(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    TypedSampleHandler handler)
{
#if defined(DDSCPP_ENABLE_RTI)
    return impl_->subscribe(topic, sample_type, std::move(handler));
#else
    (void)topic;
    (void)sample_type;
    (void)handler;
    throw DdsOperationException("RTI Connext DDS support is disabled.");
#endif
}

void RtiDdsTransport::publish_typed(
    const TopicDefinition& topic,
    const std::type_info& sample_type,
    const void* sample)
{
#if defined(DDSCPP_ENABLE_RTI)
    impl_->publish(topic, sample_type, sample);
#else
    (void)topic;
    (void)sample_type;
    (void)sample;
    throw DdsOperationException("RTI Connext DDS support is disabled.");
#endif
}

} // namespace dds_cpp
