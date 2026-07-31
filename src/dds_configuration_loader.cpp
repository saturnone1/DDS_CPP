#include "dds_cpp/dds_configuration_loader.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <unordered_set>

#include "dds_cpp/dds_exception.hpp"
#include "dds_path_utils.hpp"

namespace dds_cpp {
namespace {

using detail::require_file;

std::string read_all(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw DdsConfigurationException("File does not exist or cannot be read: " + path);
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
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

std::vector<std::string> find_tags(const std::string& xml, const std::string& tag_name)
{
    std::vector<std::string> tags;
    const auto needle = "<" + tag_name;
    std::size_t search_from = 0;

    while (true) {
        const auto begin = xml.find(needle, search_from);
        if (begin == std::string::npos) {
            break;
        }

        const auto after_name = begin + needle.size();
        if (after_name < xml.size()) {
            const auto next = xml[after_name];
            if (next != ' ' && next != '\t' && next != '\r' && next != '\n' && next != '>' && next != '/') {
                search_from = after_name;
                continue;
            }
        }

        const auto end = xml.find('>', after_name);
        if (end == std::string::npos) {
            throw DdsConfigurationException("Malformed <" + tag_name + "> tag is missing '>'.");
        }

        tags.push_back(xml.substr(begin, end - begin + 1));
        search_from = end + 1;
    }

    return tags;
}

std::string without_comments(std::string xml)
{
    std::size_t search_from = 0;
    while (true) {
        const auto begin = xml.find("<!--", search_from);
        if (begin == std::string::npos) {
            return xml;
        }

        const auto end = xml.find("-->", begin + 4);
        if (end == std::string::npos) {
            throw DdsConfigurationException("XML contains an unterminated comment.");
        }

        xml.erase(begin, end - begin + 3);
        search_from = begin;
    }
}

void require_root_element(const std::string& xml, const std::string& name, const std::string& source)
{
    std::size_t root = 0;
    while ((root = xml.find('<', root)) != std::string::npos &&
           root + 1 < xml.size() && (xml[root + 1] == '?' || xml[root + 1] == '!')) {
        const auto end = xml.find('>', root + 2);
        if (end == std::string::npos) break;
        root = end + 1;
    }
    const auto opening = "<" + name;
    const auto after_name = root == std::string::npos ? std::string::npos : root + opening.size();
    const auto correct_opening = root != std::string::npos && xml.compare(root, opening.size(), opening) == 0 &&
        after_name < xml.size() &&
        (std::isspace(static_cast<unsigned char>(xml[after_name])) != 0 || xml[after_name] == '>');
    if (!correct_opening || xml.find("</" + name + ">", after_name) == std::string::npos) {
        throw DdsConfigurationException(source + " root element must be <" + name + ">.");
    }
}

std::string attribute_value(const std::string& tag, const std::string& name)
{
    std::size_t key_begin = 0;
    while ((key_begin = tag.find(name, key_begin)) != std::string::npos) {
        const auto before_is_boundary = key_begin > 0 &&
            std::isspace(static_cast<unsigned char>(tag[key_begin - 1])) != 0;
        const auto after = key_begin + name.size();
        const auto after_is_boundary = after < tag.size() &&
            (std::isspace(static_cast<unsigned char>(tag[after])) != 0 || tag[after] == '=');
        if (before_is_boundary && after_is_boundary) {
            break;
        }
        key_begin += name.size();
    }
    if (key_begin == std::string::npos) {
        throw DdsConfigurationException("Tag is missing required '" + name + "' attribute: " + tag);
    }

    auto cursor = key_begin + name.size();
    while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])) != 0) {
        ++cursor;
    }

    if (cursor >= tag.size() || tag[cursor] != '=') {
        throw DdsConfigurationException("Tag has malformed '" + name + "' attribute: " + tag);
    }

    ++cursor;
    while (cursor < tag.size() && std::isspace(static_cast<unsigned char>(tag[cursor])) != 0) {
        ++cursor;
    }

    if (cursor >= tag.size() || (tag[cursor] != '"' && tag[cursor] != '\'')) {
        throw DdsConfigurationException("Tag has malformed '" + name + "' attribute: " + tag);
    }

    const auto quote = tag[cursor++];
    const auto value_begin = cursor;
    const auto value_end = tag.find(quote, value_begin);
    if (value_end == std::string::npos) {
        throw DdsConfigurationException("<topic> has an unterminated '" + name + "' attribute.");
    }

    return decode_xml(tag.substr(value_begin, value_end - value_begin));
}

TopicDirection parse_direction(const std::string& value, const std::string& topic_name)
{
    if (value == "Both") {
        return TopicDirection::Both;
    }
    if (value == "Publish") {
        return TopicDirection::Publish;
    }
    if (value == "Subscribe") {
        return TopicDirection::Subscribe;
    }

    throw DdsConfigurationException(
        "Topic '" + topic_name + "' has invalid direction '" + value + "'. Use Both, Publish, or Subscribe.");
}

std::unordered_set<std::string> load_qos_profiles(const std::string& qos_profiles_xml_path)
{
    const auto xml = without_comments(read_all(qos_profiles_xml_path));
    require_root_element(xml, "dds", "qos_profiles.xml");
    constexpr auto library_name = "AmbassadorProfiles";
    std::size_t library_body_begin = std::string::npos;
    for (const auto& tag : find_tags(xml, "qos_library")) {
        if (attribute_value(tag, "name") == library_name) {
            const auto library_start = xml.find(tag);
            library_body_begin = library_start + tag.size();
            break;
        }
    }

    if (library_body_begin == std::string::npos) {
        throw DdsConfigurationException(
            "qos_profiles.xml must contain <qos_library name=\"AmbassadorProfiles\">.");
    }

    const auto library_end = xml.find("</qos_library>", library_body_begin);
    if (library_end == std::string::npos) {
        throw DdsConfigurationException(
            "QoS library 'AmbassadorProfiles' is missing a closing </qos_library> tag.");
    }

    const auto library_body = xml.substr(library_body_begin, library_end - library_body_begin);
    std::unordered_set<std::string> profiles;

    for (const auto& tag : find_tags(library_body, "qos_profile")) {
        profiles.insert(attribute_value(tag, "name"));
    }

    if (profiles.empty()) {
        throw DdsConfigurationException(
            "QoS library 'AmbassadorProfiles' must contain at least one <qos_profile name=\"...\">.");
    }

    return profiles;
}

std::unordered_set<std::string> load_msg_structs(const std::string& dds_sim_xml_path)
{
    const auto xml = without_comments(read_all(dds_sim_xml_path));
    require_root_element(xml, "dds", "DDSSim.xml");
    const auto module_tags = find_tags(xml, "module");

    std::size_t module_start = std::string::npos;
    std::size_t module_body_begin = std::string::npos;
    for (const auto& tag : module_tags) {
        if (attribute_value(tag, "name") == "MSG") {
            module_start = xml.find(tag);
            module_body_begin = module_start + tag.size();
            break;
        }
    }

    if (module_start == std::string::npos) {
        throw DdsConfigurationException("DDSSim.xml must contain a <module name=\"MSG\"> section.");
    }

    const auto module_end = xml.find("</module>", module_body_begin);
    if (module_end == std::string::npos) {
        throw DdsConfigurationException("DDSSim.xml MSG module is missing a closing </module> tag.");
    }

    const auto module_body = xml.substr(module_body_begin, module_end - module_body_begin);
    std::unordered_set<std::string> message_types;

    for (const auto& tag : find_tags(module_body, "struct")) {
        message_types.insert(attribute_value(tag, "name"));
    }

    if (message_types.empty()) {
        throw DdsConfigurationException("DDSSim.xml MSG module must contain at least one struct.");
    }

    return message_types;
}

std::vector<TopicDefinition> load_topics(
    const std::string& topics_xml_path,
    const std::unordered_set<std::string>& qos_profiles,
    const std::optional<std::unordered_set<std::string>>& message_types)
{
    const auto xml = without_comments(read_all(topics_xml_path));
    require_root_element(xml, "topics", "topics.xml");
    std::vector<TopicDefinition> topics;
    std::unordered_set<std::string> seen;

    for (const auto& tag : find_tags(xml, "topic")) {
        auto name = attribute_value(tag, "name");
        auto qos_profile = attribute_value(tag, "qos_profile");
        auto direction_text = attribute_value(tag, "direction");

        if (name.empty()) {
            throw DdsConfigurationException("topics.xml contains a topic with an empty name.");
        }
        if (qos_profile.empty()) {
            throw DdsConfigurationException("Topic '" + name + "' has an empty qos_profile.");
        }

        if (!seen.insert(name).second) {
            throw DdsConfigurationException("Topic '" + name + "' is duplicated in topics.xml.");
        }

        if (!qos_profiles.contains(qos_profile)) {
            throw DdsConfigurationException(
                "Topic '" + name + "' references missing QoS profile 'AmbassadorProfiles::" + qos_profile + "'.");
        }

        if (message_types.has_value() && !message_types->contains(name)) {
            throw DdsConfigurationException("Topic '" + name + "' does not match a MSG struct in DDSSim.xml.");
        }

        const auto direction = parse_direction(direction_text, name);
        topics.push_back(TopicDefinition{std::move(name), std::move(qos_profile), direction});
    }

    if (topics.empty()) {
        throw DdsConfigurationException("topics.xml must contain at least one <topic> element.");
    }

    return topics;
}

std::optional<std::string> resolve_dds_sim_path(
    const std::optional<std::string>& explicit_path,
    const std::string& topics_xml_path)
{
    if (explicit_path.has_value() && !explicit_path->empty()) {
        return require_file(*explicit_path, "dds_sim_xml_path");
    }

    const auto candidate = std::filesystem::path(topics_xml_path).parent_path() / "DDSSim.xml";
    if (std::filesystem::is_regular_file(candidate)) {
        return std::filesystem::absolute(candidate).lexically_normal().string();
    }

    return std::nullopt;
}

} // namespace

DdsConfiguration DdsConfigurationLoader::load(const DdsClientOptions& options)
{
    const auto topics_path = detail::resolve_required_file(
        options.topics_xml_path, "definitions/topics.xml", "topics_xml_path");
    const auto qos_path = detail::resolve_required_file(
        options.qos_profiles_xml_path, "definitions/qos_profiles.xml", "qos_profiles_xml_path");
    return load(topics_path, qos_path, options.dds_sim_xml_path);
}

DdsConfiguration DdsConfigurationLoader::load(
    const std::string& topics_xml_path,
    const std::string& qos_profiles_xml_path,
    const std::optional<std::string>& dds_sim_xml_path)
{
    const auto topics_path = require_file(topics_xml_path, "topics_xml_path");
    const auto qos_profiles_path = require_file(qos_profiles_xml_path, "qos_profiles_xml_path");
    const auto dds_sim_path = resolve_dds_sim_path(dds_sim_xml_path, topics_path);

    auto qos_profiles = load_qos_profiles(qos_profiles_path);
    auto message_types = dds_sim_path.has_value()
        ? std::make_optional(load_msg_structs(*dds_sim_path))
        : std::optional<std::unordered_set<std::string>>{};

    return DdsConfiguration(load_topics(topics_path, qos_profiles, message_types));
}

} // namespace dds_cpp
