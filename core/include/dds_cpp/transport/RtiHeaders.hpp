#pragma once
// Windows SDK의 MSG 이름과 메시지 네임스페이스가 충돌하지 않도록 헤더 구간에서 이름을 보호한다.
#if defined(_WIN32)
#define MSG DDSCPP_WIN32_MSG
#endif
#include <dds/dds.hpp>
#if defined(_WIN32)
#undef MSG
#endif
