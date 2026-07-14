#pragma once

#include <string>

#include "dds_cpp/topic_direction.hpp"
#include "dds_cpp/visibility.hpp"

namespace dds_cpp {

struct DDSCPP_API TopicDefinition final {
    std::string name;
    std::string qos_profile;
    TopicDirection direction{TopicDirection::Both};

    [[nodiscard]] std::string qualified_qos_profile() const
    {
        return "AmbassadorProfiles::" + qos_profile;
    }

    [[nodiscard]] std::string QualifiedQosProfile() const
    {
        return qualified_qos_profile();
    }
    [[nodiscard]] const std::string& Name() const noexcept { return name; }
    [[nodiscard]] const std::string& QosProfile() const noexcept { return qos_profile; }
    [[nodiscard]] TopicDirection Direction() const noexcept { return direction; }
};

} // namespace dds_cpp
