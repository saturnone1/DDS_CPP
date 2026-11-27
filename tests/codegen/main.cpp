#include "Nested_messages.hpp"
#include "GlobalA_messages.hpp"
#include "GlobalB_messages.hpp"
#include "Keyword_messages.hpp"
#include <type_traits>
#include <iostream>
int main() {
    DdsModules_Nested::Outer::Common::NestedStatus nested;
    DdsModules_Nested::Common::RootStatus root;
    DdsModules_Keyword::event::Status keyword;
    DdsModules_GlobalA::Common::Status a;
    DdsModules_GlobalB::Common::Status b;
    static_assert(!std::is_same_v<DdsModules_GlobalA_GlobalMode, DdsModules_GlobalB_GlobalMode>);
    a.mode(DdsModules_GlobalA_GlobalMode::Idle);
    b.mode(DdsModules_GlobalB_GlobalMode::Idle);
    if (static_cast<int>(a.mode()) != 0 || static_cast<int>(b.mode()) != 1) return 1;
    if (dds::topic::topic_type_name<decltype(a)>::value() != "Common::Status" ||
        dds::topic::topic_type_name<decltype(b)>::value() != "Common::Status") return 2;
    std::cout << "Nested, escaped keyword, global enums and original wire names passed.\n";
}
