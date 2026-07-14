# DDSCPP

C++20 DDS client library backed by RTI Connext DDS. `DDSSim.xml` is converted
to Modern C++ types during the build, and `topics.xml` generates the runtime
type registry used by the transport.

## Prerequisites

- RTI Connext DDS 7.3.1 or a compatible installation
- A valid RTI license
- CMake 3.20+
- Visual Studio on Windows

The build locates RTI through `DDSCPP_RTI_HOME`, `NDDSHOME`, or the newest
`C:/Program Files/rti_connext_dds-*` installation.

## Build

```powershell
cmake -S DDSCPP -B DDSCPP/build -DDDSCPP_ENABLE_RTI=ON
cmake --build DDSCPP/build --config Release
ctest --test-dir DDSCPP/build -C Release --output-on-failure
```

Use `-DDDSCPP_ENABLE_RTI=OFF` for an in-memory-only build. RTI builds generate
`DDSSim.hpp`, `DDSSim.cxx`, and the topic registry under `build/generated`.
Required RTI DLLs are copied beside the DDSCPP shared library and test binary.

### Rocky Linux 9.7

Install the matching RTI host tools and an `x64Linux*` or `arm64Linux*` target
package, then point `NDDSHOME` at that installation:

```bash
export NDDSHOME=/opt/rti_connext_dds-7.3.1
cmake -S DDSCPP -B DDSCPP/build-linux \
  -DCMAKE_BUILD_TYPE=Release -DDDSCPP_ENABLE_RTI=ON
cmake --build DDSCPP/build-linux
ctest --test-dir DDSCPP/build-linux --output-on-failure
cmake --install DDSCPP/build-linux --prefix DDSCPP/artifacts/rocky9.7
```

The Linux shared library records the RTI platform directory as its build RPATH.
For deployment, place `libnddscpp2.so`, `libnddsc.so`, and `libnddscore.so`
beside the application or expose their directory through `LD_LIBRARY_PATH`.

When code generation runs on a Windows host and compilation runs in Linux, pass
the Windows-generated directory to Linux CMake:

```bash
cmake -S DDSCPP -B DDSCPP/build-linux \
  -DDDSCPP_ENABLE_RTI=ON -DDDSCPP_RTI_HOME=/opt/rti_connext_dds-7.3.1 \
  -DDDSCPP_PREGENERATED_DIR=/src/DDSCPP/build/generated
```

An in-memory Linux container build that does not require proprietary RTI files
is also provided:

```bash
docker build -f DDSCPP/Dockerfile.linux -t ddscpp-linux .
```

Override the base image for a compatibility build, for example:

```bash
docker build --build-arg ROCKY_VERSION=10 -f DDSCPP/Dockerfile.linux \
  -t ddscpp-rocky10 .
```

The container build uses `rockylinux/rockylinux:9.7`. RTI Connext DDS 7.3.1
officially supports the equivalent Red Hat Enterprise Linux 9 platform through
the `x64Linux4gcc7.3.0` target package. The project and RTI 7.3.1 libraries have
also been build-and-runtime tested on Rocky Linux 10 with GCC 14.3/glibc 2.39,
but Rocky/RHEL 10 is not an officially listed Connext 7.3.1 target. Use a
Connext release that explicitly supports RHEL 10 when vendor-supported Rocky 10
production deployment is required.

## Definitions workflow

Edit the files under `DDS_Nuget/definitions`:

- `DDSSim.xml`: enum, struct, sequence, array, and message definitions
- `topics.xml`: topic name, QoS profile, and publish/subscribe direction
- `qos_profiles.xml`: RTI DataWriter/DataReader QoS
- `dds_client*.xml`: domain, transport, participant, peers, and file paths

Then rebuild. Changes to `DDSSim.xml` or `topics.xml` automatically rerun
`rtiddsgen` and regenerate the C++ registry. Every `<topic name="X">` must have
a matching `MSG::X` struct in `DDSSim.xml`.

## NuGet-compatible typed API

```cpp
#include <dds_cpp/dds_cpp.hpp>

auto client = dds_cpp::DdsClient::Connect(
    "DDS_Nuget/definitions/dds_client.rti-multicast.xml");

auto subscription = client.Subscribe<MSG::TimeTickInformation>(
    [](const MSG::TimeTickInformation& sample) {
        std::cout << sample.TimeTickMessage().TimeTick() << '\n';
    });

MSG::TimeTickInformation sample;
sample.TimeTickMessage().TimeTick(1);
sample.TimeTickMessage().SyncCycle(0);
client.Publish(sample);
```

The public workflow and method names mirror the NuGet package. Topic names are
resolved from the unqualified C++ type name, exactly like C# `typeof(T).Name`.
`CreatePublisher<T>()`, `CreateSubscriber<T>()`, explicit-topic overloads,
direction validation, configuration access, and publisher handles are also
available. See [docs/api-parity.md](docs/api-parity.md) for the complete mapping.
한국어 설치·연결·API 예제는 [docs/library-usage.md](docs/library-usage.md)를
참고하세요.

## NuGet-compatible CLI

RTI builds also produce `ddsclient` (`ddsclient.exe` on Windows) with the same
commands and aliases as `DdsAmbassador.DDSClient.Cli`:

```text
ddsclient list [--config <path>]
ddsclient shell [--config <path>]
ddsclient idle [--config <path>]
ddsclient publish <MessageName> [--config <path>] [--json <path>|--stdin] [--repeat <n>] [--interval-ms <n>]
ddsclient subscribe <MessageName> [--config <path>] [--count <n>] [--timeout-ms <n>]
```

The shell supports `list`, `status`, `publish`/`send`, `subscribe`/`listen`,
`unsubscribe`/`unsub`, `help`, and `exit`. JSON is converted through the RTI
DynamicData type generated from `DDSSim.xml`, so nested fields and enums use the
same message definition rather than a hand-written topic-specific parser.
`--repeat`/`--interval-ms` keep one publisher participant alive for deterministic
tests. `--count`/`--timeout-ms` make a subscriber exit with success after the
requested samples or fail on timeout.

Generated RTI types are available through `dds_cpp/generated_types.hpp`, which
is included by `dds_cpp/dds_cpp.hpp`. Raw byte publish/subscribe remains
available for the in-memory transport; RTI requires generated typed messages.

Configuration values from environment variables override XML values. Supported
variables include `DDS_CLIENT_CONFIG_PATH`, `DDS_DOMAIN_ID`,
`DDS_USE_RTI_TRANSPORT`, `DDS_PARTICIPANT_NAME`, `DDS_INITIAL_PEERS`,
`DDS_TOPICS_XML_PATH`, `DDS_QOS_PROFILES_XML_PATH`, `DDS_DDSSIM_XML_PATH`, and
`DDS_LOG_LEVEL`.

Configuration resolution is intentionally simple:

- `Connect()` and `Connect(path)` call `DdsClientOptions::Load`, so environment
  variables override XML values.
- `Connect(options)` uses the supplied options exactly. It does not silently
  read environment variables again.
- Empty definition paths in direct options use `definitions` beside the
  executable, under the current directory, or in an installed
  `share/dds_cpp/definitions` directory.
