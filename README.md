# device-driver-binding-explainer

> Release context: see [qualified platforms, runtime requirements and package scope](RELEASE-QUALIFICATION.md).


Version 1.0.0. A portable, strictly read-only Windows command-line utility that explains a Plug and Play device's configured driver association. C++17, CMake, MIT licensed. No telemetry or network access.

## Usage

```text
device-driver-binding-explainer list
device-driver-binding-explainer list --class Net --json
device-driver-binding-explainer list --match PCI --limit 20
device-driver-binding-explainer list --all --limit 0
device-driver-binding-explainer device "PCI\VEN_1234&DEV_5678\4&12345678&0&0000"
device-driver-binding-explainer device "ROOT\EXAMPLE\0000" --json
device-driver-binding-explainer --help
device-driver-binding-explainer --version
```

Discover the exact instance ID with `list`; example IDs above are placeholders. Quote IDs, especially IDs containing `&`. List defaults to present devices and at most 100 matching rows. `--all` includes registered non-present devices; `--limit 0` removes the output limit. `--match` matches an instance ID or display name substring; `--class` matches a class name exactly, both case-insensitively. Results are sorted by instance ID. Enumeration still visits every device to provide an accurate match count.

Device output includes name, description, manufacturer, class/GUID, enumerator, parent, presence, raw Configuration Manager status flags and decoded flags/problem code, hardware and compatible IDs, driver key reference, INF name/section, provider/date/version, configured matching ID/rank, and service name/configuration. Driver dates are UTC calendar dates from the supplied FILETIME, not installation times. Version strings preserve provider metadata. Service start/type/error-control values use Windows SCM numeric constants; image paths are raw configured strings, which may contain environment variables or arguments. They are never resolved or executed.

## Current binding and Windows ranking

Properties describe current configuration and reported metadata. `configured_rank` and `matching_device_id` come from the supported unified device property interface when available. They do not reproduce Windows' full candidate enumeration/ranking process. No candidate driver list is built; no installation/selection API is called.

The observed INF was not necessarily the only eligible package. Windows may choose a different package after reinstall. Version alone does not determine ranking. Configured service information does not prove that the driver is loaded, running, or functioning. A Configuration Manager status flag is an observation, not a health guarantee. No reported problem does not prove hardware health; missing metadata does not mean no driver exists. Driver key is a reference, not a verified driver-store location. INF association is metadata, not verification of INF content or package integrity. No signer or trust assertion is made: cryptographic verification is explicitly unsupported.

## Safety and network policy

The executable uses only local SetupAPI property/enumeration queries, Configuration Manager queries, and SCM queries with `SC_MANAGER_CONNECT` / `SERVICE_QUERY_CONFIG`. It never installs, updates, rolls back, removes, enables, disables, restarts, re-enumerates, binds, unbinds, stages, or unstages anything. It never writes device/driver registry configuration or changes the driver store. It does not request a reboot. If Windows reports a problem code mentioning a restart, this is reported metadata only.

No installation functions, `DIF_PROPERTYCHANGE`, service mutations, registry writes, external commands, child processes, sockets, DNS, HTTP, Windows Update, connectivity checks, online driver searches, or telemetry are used. The application never calls pnputil, devcon, PowerShell, cmd.exe, DISM, or vendor updaters. Repository build/validation scripts are developer tools, separate from the executable. Output goes to stdout/stderr; redirecting output is a caller choice.

## JSON and exit codes

`--json` emits one UTF-8 JSON object, without BOM, to stdout, including errors. Field order is deterministic; device state itself can change between runs. `schema_version` is 1 and `application_version` is `1.0.0`. Device results have `device`, `identifiers`, `driver_association`, and explanatory notes. List results have `devices`, `matched_count`, `shown_count`, and `truncated`.

Each property has this shape:

```json
{"availability":"unavailable","value":null,"error":{"domain":"win32","code":1168},"reason":"Metadata was not supplied or could not be found."}
```

Availability is `available`, `unavailable`, `access_denied`, `unsupported`, or `not_applicable`. `value` is a string, number, boolean, string array, or null. Hardware/compatible IDs are arrays. Known-empty values remain `""` or `[]`; missing values are null. Error is null or a numeric code with domain `win32` or `configret`. Not-applicable is used only when a known-empty service name prevents a meaningful service query; missing service metadata remains unavailable. Unsupported can mean an OS/interface limitation or explicitly unimplemented trust verification. Property source errors are preserved where available.

Exit codes: 0 successful query (including partially unavailable properties); 2 invalid arguments/identifier; 3 device not found; 4 query, enumeration, conversion, or output failure. Human-readable errors use stderr. JSON errors use stdout with an `error` object. A list with no matches is successful.

## Permissions, compatibility, and limitations

Runs as the caller with no elevation request. Ordinary device metadata generally needs no administrator privileges; ACLs may deny service/property reads. Those reads are reported per field, never retried with elevated privileges.

Target baseline is Windows 10 and Windows Server 2016, with Windows 11 and later supported Windows Server generations using the same local APIs. The OS being used for development does not establish the baseline. All imported query APIs predate this baseline; no newer optional API is required. Runtime query errors/types are checked, including unsupported properties, rather than assuming every OS/device provides every field. The release is x64; other Windows architectures require rebuilding with the appropriate compiler. Actual validation is recorded in [VALIDATION.md](VALIDATION.md); compatibility on untested versions is an engineering target, not a test claim.

Reads are not atomic. Devices may disappear or configuration may change during enumeration. Some devices have no hardware IDs, service, INF association, or friendly name. The description is used as display-name fallback. Non-present devices may lack a live Configuration Manager devnode. Property size is limited to 1 MiB and queries retry size races at most four times. Malformed property data becomes unavailable. Unknown problem codes preserve their number. Unrecognized status bits retain their numeric mask. No remote computers, INF parsing, package enumeration, driver-store inspection, service runtime status, or signature validation are performed.

## Build and test

Install CMake 3.20+, a C++17 Windows compiler and Windows SDK. From a Visual Studio developer terminal:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Visual Studio generators also work; supply `--config Release` to build and `-C Release` to CTest. MSVC project code uses `/W4 /WX /permissive-`. The CRT is statically linked for a portable executable. Configure `-DDDBE_ANALYZE=ON` for `/analyze`. `scripts/build.ps1` is a convenience for this validation host with explicit compiler/SDK paths; edit paths for your installation or use a developer terminal.

CTest covers bounded MULTI_SZ parsing, ID arrays, Unicode, version/date formatting, CM problems/status bits, JSON escaping/availability, malformed identifiers and CLI options. CLI contract tests parse actual output with CMake's JSON parser and check exit codes. The default live tests inspect every ordinary present device, check all-device enumeration and a deliberately nonexistent ID, and parse live CLI JSON. They never modify a device. Disable host-dependent live tests with `-DDDBE_LIVE_TESTS=OFF`.

For static analysis, use clang-tidy with the compilation database (`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`) and the Windows compiler/SDK include environment, and Cppcheck with `--platform=win64 --std=c++17 --enable=warning,style,performance,portability --error-exitcode=1 -Isrc src tests`. Validation details and any tool constraints belong in VALIDATION.md.

## Portable package

```text
cd build
cpack -G ZIP -C Release
```

The ZIP contains the executable, README, MIT license, and validation record. Extract and run; no installer, driver installation, configuration file, or VC runtime installation is needed. Release binaries are unsigned. No package signing/trust claim is implied.

For this workspace, `scripts/package.ps1` writes the ZIP and SHA256SUMS.txt under `dist/`. An `asInvoker` manifest is embedded; it never requests administrator elevation.
