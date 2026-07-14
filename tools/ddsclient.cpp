#include "dds_cpp/dds_cpp.hpp"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dds/dds.hpp>
#include <ndds/ndds_c.h>
#include <ndds/dds_c/dds_c_dynamicdataformatter.h>
#include <rti/apputils/DynamicDataConverter.hpp>

namespace {

struct CliOptions {
    std::string config_path;
    std::string topic;
    std::string json_path;
    bool read_stdin{};
    int count{};
    int timeout_ms{};
    int repeat{1};
    int interval_ms{};
};

std::atomic_bool stopped{false};

void on_signal(int) { stopped = true; }

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string read_value(const std::vector<std::string>& args, std::size_t& index)
{
    if (++index >= args.size()) throw std::invalid_argument(args[index - 1] + " 옵션에는 값이 필요합니다.");
    return args[index];
}

int read_integer(const std::vector<std::string>& args, std::size_t& index, bool positive)
{
    const auto option = args[index];
    const auto value = read_value(args, index);
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(value, &consumed);
        if (consumed != value.size() || parsed > (std::numeric_limits<int>::max)() ||
            (positive ? parsed <= 0 : parsed < 0)) throw std::invalid_argument("range");
        return static_cast<int>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(option + (positive
            ? " 옵션에는 1 이상의 정수가 필요합니다."
            : " 옵션에는 0 이상의 정수가 필요합니다."));
    }
}

CliOptions parse_options(const std::vector<std::string>& args)
{
    CliOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--config" || args[i] == "-c") options.config_path = read_value(args, i);
        else if (args[i] == "--json" || args[i] == "-j") options.json_path = read_value(args, i);
        else if (args[i] == "--stdin") options.read_stdin = true;
        else if (args[i] == "--count") options.count = read_integer(args, i, false);
        else if (args[i] == "--timeout-ms") options.timeout_ms = read_integer(args, i, false);
        else if (args[i] == "--repeat") options.repeat = read_integer(args, i, true);
        else if (args[i] == "--interval-ms") options.interval_ms = read_integer(args, i, false);
        else if (options.topic.empty()) options.topic = args[i];
    }
    return options;
}

dds_cpp::DdsClient connect(const CliOptions& options)
{
    return options.config_path.empty()
        ? dds_cpp::DdsClient::Connect()
        : dds_cpp::DdsClient::Connect(options.config_path);
}

std::string read_json(const CliOptions& options)
{
    if (!options.json_path.empty()) {
        std::ifstream input(options.json_path, std::ios::binary);
        if (!input) throw std::invalid_argument("JSON 파일을 읽을 수 없습니다: " + options.json_path);
        std::ostringstream content;
        content << input.rdbuf();
        return content.str();
    }
    if (options.read_stdin) {
        std::ostringstream content;
        content << std::cin.rdbuf();
        return content.str();
    }
    return {};
}

std::string normalize_json_names(std::string json)
{
    static const std::unordered_map<std::string, std::string> canonical_names = [] {
        std::unordered_map<std::string, std::string> names;
#define DDSCPP_JSON_NAME(name) names.emplace(lower(name), name);
#include "dds_cpp_json_names.inc"
#undef DDSCPP_JSON_NAME
        return names;
    }();

    std::size_t cursor = 0;
    while ((cursor = json.find('"', cursor)) != std::string::npos) {
        const auto value_begin = ++cursor;
        bool escaped = false;
        while (cursor < json.size()) {
            if (json[cursor] == '\\') { escaped = true; cursor += 2; continue; }
            if (json[cursor] == '"') break;
            ++cursor;
        }
        if (cursor >= json.size()) break;
        if (!escaped) {
            const auto value = json.substr(value_begin, cursor - value_begin);
            if (const auto found = canonical_names.find(lower(value)); found != canonical_names.end()) {
                json.replace(value_begin, cursor - value_begin, found->second);
                cursor = value_begin + found->second.size();
            }
        }
        ++cursor;
    }
    return json;
}

bool contains_json_member(const std::string& json, const std::string& member_name)
{
    if (json.empty()) return false;
    const auto normalized = normalize_json_names(json);
    const auto needle = "\"" + member_name + "\"";
    std::size_t cursor = 0;
    while ((cursor = normalized.find(needle, cursor)) != std::string::npos) {
        auto after = cursor + needle.size();
        while (after < normalized.size() && std::isspace(static_cast<unsigned char>(normalized[after]))) ++after;
        if (after < normalized.size() && normalized[after] == ':') return true;
        cursor = after;
    }
    return false;
}

template <class T>
T from_json(const std::string& json)
{
    T sample;
    if (json.empty()) return sample;

    const auto& type = rti::topic::dynamic_type<T>::get();
    dds::core::xtypes::DynamicData dynamic_sample(type);
    const auto normalized_json = normalize_json_names(json);
    if (DDS_DynamicDataFormatter_from_json(&dynamic_sample.native(), normalized_json.c_str()) != DDS_RETCODE_OK) {
        throw std::invalid_argument("JSON을 요청한 DDS 메시지 타입으로 변환할 수 없습니다.");
    }

    rti::apputils::DynamicDataConverter converter(
        type, rti::apputils::DynamicBufferProperty(64 * 1024, false));
    auto buffer = converter.buffer(dynamic_sample, DDS_XCDR_DATA_REPRESENTATION);
    const auto* begin = buffer.get<const char>();
    std::vector<char> cdr(begin, begin + buffer.length());
    using topic_support = dds::topic::topic_type_support<T>;
    topic_support::from_cdr_buffer(sample, cdr);
    return sample;
}

ENUM::SimulatorID simulator_id(const char* variable, ENUM::SimulatorID default_value)
{
    const auto* value = std::getenv(variable);
    if (value == nullptr || *value == '\0') return default_value;
    const auto selected = lower(value);
#define DDSCPP_SIM_ID(name) if (selected == lower(#name)) return ENUM::SimulatorID::name
    DDSCPP_SIM_ID(TCC); DDSCPP_SIM_ID(TDS); DDSCPP_SIM_ID(VGD); DDSCPP_SIM_ID(DOLU);
    DDSCPP_SIM_ID(ELU); DDSCPP_SIM_ID(ATS_A); DDSCPP_SIM_ID(ATS_B); DDSCPP_SIM_ID(ATS_C);
    DDSCPP_SIM_ID(ATS_D); DDSCPP_SIM_ID(MSS_A); DDSCPP_SIM_ID(MSS_B);
#undef DDSCPP_SIM_ID
    return default_value;
}

template <class T>
void apply_defaults(T& sample, ENUM::MessageID message_id, const std::string& json)
{
    if constexpr (requires(T& value) { value.Header(); }) {
        auto& header = sample.Header();
        if (header.TimeStamp() == 0) {
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            header.TimeStamp(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
        }
        if (!contains_json_member(json, "MsgID") || static_cast<int>(header.MsgID()) == 0) {
            header.MsgID(message_id);
        }
        if (!contains_json_member(json, "SrcSimID") || static_cast<int>(header.SrcSimID()) == 0) {
            header.SrcSimID(simulator_id("DDS_CLI_SRC_SIM_ID", ENUM::SimulatorID::TCC));
        }
        if (!contains_json_member(json, "DstSimID") || static_cast<int>(header.DstSimID()) == 0) {
            header.DstSimID(simulator_id("DDS_CLI_DST_SIM_ID", ENUM::SimulatorID::TDS));
        }
    }
}

void publish(dds_cpp::DdsClient& client, const CliOptions& options)
{
    const auto json = read_json(options);
#define DDSCPP_REGISTER_RTI_TYPE(cpp_type, enum_name, dds_name) \
    if (options.topic == dds_name) { \
        auto sample = from_json<cpp_type>(json); \
        apply_defaults(sample, ENUM::MessageID::enum_name, json); \
        for (int index = 0; index < options.repeat; ++index) { \
            client.Publish(options.topic, sample); \
            if (index + 1 < options.repeat && options.interval_ms > 0) \
                std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms)); \
        } \
        std::cout << "송신 완료: " << options.topic << " (MSG::" << dds_name << "), count=" << options.repeat << "\n"; \
        return; \
    }
#include "dds_cpp_rti_type_registry.inc"
#undef DDSCPP_REGISTER_RTI_TYPE
    throw dds_cpp::DdsConfigurationException("MSG." + options.topic + " 타입을 찾을 수 없습니다.");
}

std::unique_ptr<dds_cpp::Subscription> subscribe(
    dds_cpp::DdsClient& client,
    const std::string& topic,
    std::function<void()> on_sample = {})
{
#define DDSCPP_REGISTER_RTI_TYPE(cpp_type, enum_name, dds_name) \
    if (topic == dds_name) { \
        return client.Subscribe<cpp_type>(topic, [topic, on_sample](const cpp_type& sample) { \
            std::cout << '\n' << topic << '\n' << sample << std::endl; \
            if (on_sample) on_sample(); \
        }); \
    }
#include "dds_cpp_rti_type_registry.inc"
#undef DDSCPP_REGISTER_RTI_TYPE
    throw dds_cpp::DdsConfigurationException("MSG." + topic + " 타입을 찾을 수 없습니다.");
}

void print_topics(const dds_cpp::DdsConfiguration& configuration)
{
    auto topics = configuration.Topics();
    std::sort(topics.begin(), topics.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    for (const auto& topic : topics) {
        const char* direction = topic.direction == dds_cpp::TopicDirection::Both ? "Both"
            : topic.direction == dds_cpp::TopicDirection::Publish ? "Publish" : "Subscribe";
        std::cout << topic.name << " direction=" << direction
                  << " qos=" << topic.QualifiedQosProfile() << '\n';
    }
}

std::vector<std::string> split_line(const std::string& line)
{
    std::vector<std::string> result;
    std::string token;
    bool quoted = false;
    for (const char ch : line) {
        if (ch == '"') quoted = !quoted;
        else if (std::isspace(static_cast<unsigned char>(ch)) && !quoted) {
            if (!token.empty()) { result.push_back(std::move(token)); token.clear(); }
        } else token += ch;
    }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

void print_shell_help()
{
    std::cout << "list | status | subscribe <topic> | publish <topic> [--json path] | "
                 "unsubscribe <topic|all> | help | exit\n";
}

int run_shell(const CliOptions& options)
{
    auto client = connect(options);
    std::unordered_map<std::string, std::unique_ptr<dds_cpp::Subscription>> subscriptions;
    std::cout << "DDS Client Shell - domain=" << client.Options().DomainId() << '\n';
    print_shell_help();
    std::string line;
    while (!stopped) {
        std::cout << "ddsclient(도움말)> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        auto tokens = split_line(line);
        if (tokens.empty()) continue;
        try {
            const auto command = lower(tokens.front());
            if (command == "exit" || command == "quit") break;
            if (command == "help" || command == "?") print_shell_help();
            else if (command == "list") print_topics(client.Configuration());
            else if (command == "status") {
                std::cout << "domain=" << client.Options().DomainId()
                          << ", 수신 중인 topic=" << subscriptions.size() << '\n';
            } else if (command == "publish" || command == "send") {
                publish(client, parse_options(std::vector<std::string>(tokens.begin() + 1, tokens.end())));
            } else if (command == "subscribe" || command == "listen") {
                const auto nested = parse_options(std::vector<std::string>(tokens.begin() + 1, tokens.end()));
                if (nested.topic.empty()) throw std::invalid_argument("메시지/topic 이름이 필요합니다.");
                if (nested.read_stdin) throw std::invalid_argument("shell 모드에서는 --stdin을 지원하지 않습니다.");
                if (!subscriptions.contains(nested.topic)) subscriptions[nested.topic] = subscribe(client, nested.topic);
            } else if (command == "unsubscribe" || command == "unsub") {
                if (tokens.size() < 2) throw std::invalid_argument("unsubscribe에는 topic 이름 또는 all이 필요합니다.");
                if (tokens[1] == "all") subscriptions.clear(); else subscriptions.erase(tokens[1]);
            } else std::cerr << "알 수 없는 shell 명령입니다: " << command << '\n';
        } catch (const std::exception& exception) {
            std::cerr << "[오류] " << exception.what() << '\n';
        }
    }
    return 0;
}

void print_usage()
{
    std::cout << "DDS Client CLI\n"
                 "  ddsclient list [--config <path>]\n"
                 "  ddsclient shell [--config <path>]\n"
                 "  ddsclient idle [--config <path>]\n"
                 "  ddsclient publish <MessageName> [--config <path>] [--json <path>|--stdin] [--repeat <n>] [--interval-ms <n>]\n"
                 "  ddsclient subscribe <MessageName> [--config <path>] [--count <n>] [--timeout-ms <n>]\n";
}

} // namespace

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    if (argc < 2) { print_usage(); return 1; }
    const std::string command = lower(argv[1]);
    if (command == "help" || command == "-h" || command == "--help") { print_usage(); return 0; }
    try {
        std::vector<std::string> args;
        for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);
        const auto options = parse_options(args);
        if (command == "list") {
            const auto client_options = options.config_path.empty()
                ? dds_cpp::DdsClientOptions::Load()
                : dds_cpp::DdsClientOptions::Load(options.config_path);
            print_topics(dds_cpp::DdsConfigurationLoader::load(client_options));
            return 0;
        }
        if (command == "shell" || command == "daemon") return run_shell(options);
        if (command == "idle" || command == "wait") {
            auto client = connect(options);
            std::cout << "DDS participant가 초기화되었습니다. domain=" << client.Options().DomainId() << '\n';
            while (!stopped) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return 0;
        }
        if (options.topic.empty()) throw std::invalid_argument("메시지/topic 이름이 필요합니다.");
        auto client = connect(options);
        if (command == "publish" || command == "send") { publish(client, options); return 0; }
        if (command == "subscribe" || command == "listen") {
            std::atomic_int received_count{0};
            auto subscription = subscribe(client, options.topic, [&] { ++received_count; });
            std::cout << "수신 대기 중: " << options.topic << ", DDS domain=" << client.Options().DomainId() << '\n';
            const auto started = std::chrono::steady_clock::now();
            while (!stopped && (options.count == 0 || received_count.load() < options.count)) {
                if (options.timeout_ms > 0 &&
                    std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(options.timeout_ms)) {
                    if (options.count > 0) throw std::runtime_error("수신 대기 시간이 초과되었습니다.");
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return 0;
        }
        std::cerr << "알 수 없는 명령입니다: " << command << '\n';
        print_usage();
        return 1;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
