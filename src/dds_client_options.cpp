#include "dds_cpp/dds_client_options.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>

#include "dds_cpp/dds_exception.hpp"
#include "dds_path_utils.hpp"

namespace dds_cpp {
namespace {

using detail::environment;

std::string trim(std::string value)
{
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), is_space));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(), value.end());
    return value;
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

void replace_all(std::string& value, const std::string_view from, const std::string_view to)
{
    std::size_t cursor = 0;
    while ((cursor = value.find(from, cursor)) != std::string::npos) {
        value.replace(cursor, from.size(), to);
        cursor += to.size();
    }
}

std::string decode_xml(std::string value)
{
    replace_all(value, "&lt;", "<");
    replace_all(value, "&gt;", ">");
    replace_all(value, "&quot;", "\"");
    replace_all(value, "&apos;", "'");
    replace_all(value, "&amp;", "&");
    return value;
}

std::string read_all(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw DdsConfigurationException("DDS client config cannot be read: " + path.string());
    }
    std::ostringstream result;
    result << stream.rdbuf();
    return result.str();
}

std::string without_comments(std::string xml)
{
    std::size_t cursor = 0;
    while ((cursor = xml.find("<!--", cursor)) != std::string::npos) {
        const auto end = xml.find("-->", cursor + 4);
        if (end == std::string::npos) {
            throw DdsConfigurationException("DDS client config contains an unterminated XML comment.");
        }
        xml.erase(cursor, end - cursor + 3);
    }
    return xml;
}

std::optional<std::string> element_value(const std::string& xml, const std::string& name)
{
    const auto open = "<" + name + ">";
    const auto close = "</" + name + ">";
    const auto begin = xml.find(open);
    if (begin == std::string::npos) {
        return std::nullopt;
    }
    const auto value_begin = begin + open.size();
    const auto end = xml.find(close, value_begin);
    if (end == std::string::npos) {
        throw DdsConfigurationException("DDS client config element '" + name + "' is not closed.");
    }
    auto value = decode_xml(trim(xml.substr(value_begin, end - value_begin)));
    return value.empty() ? std::nullopt : std::make_optional(std::move(value));
}

std::vector<std::string> element_values(const std::string& xml, const std::string& name)
{
    std::vector<std::string> values;
    const auto open = "<" + name + ">";
    const auto close = "</" + name + ">";
    std::size_t cursor = 0;
    while ((cursor = xml.find(open, cursor)) != std::string::npos) {
        const auto value_begin = cursor + open.size();
        const auto end = xml.find(close, value_begin);
        if (end == std::string::npos) {
            throw DdsConfigurationException("DDS client config element '" + name + "' is not closed.");
        }
        auto value = decode_xml(trim(xml.substr(value_begin, end - value_begin)));
        if (!value.empty()) {
            values.push_back(std::move(value));
        }
        cursor = end + close.size();
    }
    return values;
}

bool parse_bool(const std::string& value, const std::string& source)
{
    const auto normalized = lower(trim(value));
    if (normalized == "true") {
        return true;
    }
    if (normalized == "false") {
        return false;
    }
    throw DdsConfigurationException(source + " must be true or false.");
}

std::int32_t parse_domain_id(const std::string& value, const std::string& source)
{
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoll(trim(value), &consumed);
        if (consumed != trim(value).size() || parsed < std::numeric_limits<std::int32_t>::min() ||
            parsed > std::numeric_limits<std::int32_t>::max()) {
            throw std::out_of_range("domain");
        }
        return static_cast<std::int32_t>(parsed);
    } catch (const std::exception&) {
        throw DdsConfigurationException(source + " must be an integer.");
    }
}

DdsLogLevel parse_log_level(const std::string& value, const std::string& source)
{
    const auto normalized = lower(trim(value));
    if (normalized == "none") return DdsLogLevel::None;
    if (normalized == "error") return DdsLogLevel::Error;
    if (normalized == "info") return DdsLogLevel::Info;
    if (normalized == "debug") return DdsLogLevel::Debug;
    throw DdsConfigurationException(source + " must be None, Error, Info, or Debug.");
}

std::string resolve_path(const std::string& value, const std::filesystem::path& base)
{
    const std::filesystem::path path(value);
    return (path.is_absolute() ? path : base / path).lexically_normal().string();
}

std::vector<std::string> split_peers(const std::string& value)
{
    const auto normalized = lower(trim(value));
    if (normalized == "none" || normalized == "default") return {};
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(';', begin);
        auto peer = trim(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (!peer.empty()) result.push_back(std::move(peer));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

} // namespace

DdsClientOptions DdsClientOptions::from_environment()
{
    return load();
}

DdsClientOptions DdsClientOptions::load(const std::optional<std::string>& requested_config_path)
{
    DdsClientOptions options;
    const auto config = detail::resolve_config_file(requested_config_path);
    std::optional<std::filesystem::path> config_base;
    if (config.has_value()) {
        options.config_path = config->string();
        const auto xml = without_comments(read_all(*config));
        const auto base = config->parent_path();
        config_base = base;

        if (const auto value = element_value(xml, "domain_id")) options.domain_id = parse_domain_id(*value, "domain_id");
        if (const auto value = element_value(xml, "participant_name")) options.participant_name = *value;
        if (const auto value = element_value(xml, "log_level")) options.log_level = parse_log_level(*value, "log_level");
        if (const auto value = element_value(xml, "use_rti_transport")) {
            options.use_rti_transport = parse_bool(*value, "use_rti_transport");
        } else if (const auto transport_value = element_value(xml, "transport")) {
            const auto transport = lower(*transport_value);
            if (transport == "rti" || transport == "rtidds") options.use_rti_transport = true;
            else if (transport == "inmemory" || transport == "memory") options.use_rti_transport = false;
            else throw DdsConfigurationException("transport must be Rti or InMemory.");
        }
        if (const auto value = element_value(xml, "topics_xml_path")) options.topics_xml_path = resolve_path(*value, base);
        if (const auto value = element_value(xml, "qos_profiles_xml_path")) options.qos_profiles_xml_path = resolve_path(*value, base);
        if (const auto value = element_value(xml, "dds_sim_xml_path")) options.dds_sim_xml_path = resolve_path(*value, base);
        if (const auto peers_xml = element_value(xml, "initial_peers")) {
            options.initial_peers = element_values(*peers_xml, "peer");
        }
    }

    const auto environment_path = [&config_base](const std::string& value) {
        return config_base.has_value() ? resolve_path(value, *config_base) : value;
    };
    if (const auto value = environment("DDS_TOPICS_XML_PATH")) options.topics_xml_path = environment_path(*value);
    if (const auto value = environment("DDS_QOS_PROFILES_XML_PATH")) options.qos_profiles_xml_path = environment_path(*value);
    if (const auto value = environment("DDS_DDSSIM_XML_PATH")) options.dds_sim_xml_path = environment_path(*value);
    else if (const auto legacy = environment("DDS_SIM_XML_PATH")) options.dds_sim_xml_path = environment_path(*legacy);
    if (const auto value = environment("DDS_PARTICIPANT_NAME")) options.participant_name = *value;
    if (const auto value = environment("DDS_DOMAIN_ID")) options.domain_id = parse_domain_id(*value, "DDS_DOMAIN_ID");
    if (const auto value = environment("DDS_USE_RTI_TRANSPORT")) options.use_rti_transport = parse_bool(*value, "DDS_USE_RTI_TRANSPORT");
    if (const auto value = environment("DDS_LOG_LEVEL")) options.log_level = parse_log_level(*value, "DDS_LOG_LEVEL");
    if (const auto value = environment("DDS_INITIAL_PEERS")) options.initial_peers = split_peers(*value);

    return options;
}

} // namespace dds_cpp
