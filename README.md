# DDS C++ client

C++20 RTI Connext DDS core with independently generated message modules,
static/shared libraries, and CMake consumer targets. RTI Connext DDS 7.3.1
is required; the in-memory transport is an internal test implementation.

This repository is the `DDSCPP/` submodule of
[DDSClient](https://github.com/saturnone1/DDSClient). Build it from that parent
checkout: shared XML definitions, code generators, CLI sources, and build
helpers are intentionally maintained there. A standalone clone does not
contain those inputs.

## Build and test

On Windows, use PowerShell 7 with CMake, Python, Visual Studio x64 C++ tools,
and RTI Connext installed:

```powershell
git clone --recurse-submodules https://github.com/saturnone1/DDSClient.git
cd DDSClient
./build.ps1 -Language Cpp -Target Windows -WithTests -WithCli
```

For Rocky Linux and offline packaging, use the parent
[build guide](https://github.com/saturnone1/DDSClient/blob/main/docs/build.md).
RTI installation files, native libraries, and a valid runtime license must
be supplied separately under their applicable terms.

## Structure

- `core/include/dds_cpp/`: public API, configuration, and transport contracts
- `core/src/`: connection lifecycle, configuration, and RTI transport
- `cmake/`: generated message-module integration and installed package config
- `tests/`: lifecycle, delivery, network isolation, and consumer tests
- `generated/`: message modules produced from the parent's `definitions/*.xml`

Use `<dds_cpp/DdsCpp.hpp>` as the public entry point. New message schemas
produce separate modules without coupling their generated types to the core.
Connection options select modules, topics, domain, and QoS; independent
connections own their runtime resources. Shutdown exposes bounded cleanup
state instead of letting an unresponsive callback block indefinitely.

After building from the parent checkout, install with:

```powershell
cmake --install DDSCPP/build --config Release --prefix DDSCPP/artifacts/install
```

Installed consumers use `find_package(DDSClient CONFIG REQUIRED)` and the
static/shared targets documented in the parent
[C++ guide](https://github.com/saturnone1/DDSClient/blob/main/docs/cpp.md).
See also the parent [configuration guide](https://github.com/saturnone1/DDSClient/blob/main/docs/configuration.md)
and [architecture guide](https://github.com/saturnone1/DDSClient/blob/main/docs/architecture.md).
