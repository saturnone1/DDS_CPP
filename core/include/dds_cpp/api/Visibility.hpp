#pragma once

#if defined(_WIN32)
#if defined(DDSCPP_BUILDING_DLL)
#define DDSCPP_API __declspec(dllexport)
#elif defined(DDSCPP_USING_DLL)
#define DDSCPP_API __declspec(dllimport)
#else
#define DDSCPP_API
#endif
#else
#define DDSCPP_API __attribute__((visibility("default")))
#endif
