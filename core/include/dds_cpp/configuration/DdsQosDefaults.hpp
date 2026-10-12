#pragma once
namespace dds_cpp::detail {
// 기본 프리셋. 다른 망은 완전한 Library::Profile 이름을 지정한다.
inline constexpr char default_qos_library[] = "AmbassadorProfiles";
inline constexpr char default_topic_profile[] = "ReliableRealtime";
inline constexpr char default_qualified_qos_profile[] = "AmbassadorProfiles::ReliableRealtime";
}
