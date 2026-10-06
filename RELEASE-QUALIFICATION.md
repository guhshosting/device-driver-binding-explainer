# Validation and release context: device-driver-binding-explainer 1.0.0

Development/build validation: Windows Server 2025 x64. The exact executable completed physical Windows 11 Pro 25H2 x64 qualification (build 26200.9550) and Windows 10 Pro 22H2 x64 qualification in a dedicated QEMU VM (build 19045.6466). Physical Windows 10 hardware and ARM64 were not qualified. Windows 11 status: PASS, supported by the preserved physical qualification report dated 2026-10-02 and raw/run ledgers. Historical Windows 10 detailed ledgers are unavailable; completion is user supplied.

The final ZIP was assembled after executable qualification around the unchanged qualified EXE. Execution of this final assembled package on Windows is not claimed. Development validation is distinct from client qualification.

Qualified executable SHA-256: `97e39e1a865f2cb749762dfca94f124312c76f20f690bde936fe2caeb6fa14ed`.

No separately imported MSVC C++ runtime DLLs were found; Windows system libraries remain required.

Physical Windows 11 observations: Present and all-device enumeration; Net filter; no-match; malformed and nonexistent selectors; per-device text/JSON for network, storage controller, NVMe disk, Intel display, USB hub, system, audio and Bluetooth. Supplementary exact PCI Realtek Ethernet and ACPI fixed-button selectors. CIM cross-checks agree for class, provider, driver version, INF and UTC date across ten selected records. Service/binding metadata and unsupported signer verification remain scoped observations.

Recorded findings: No defect found. UTC driver dates agree after converting CIM local timestamps to UTC; the initial harness local-date comparison was corrected.

Qualification safety limits: no broad filesystem, registry or policy mutation tracing was performed. Parent-PID TCP polling observed no connections; UDP, child traffic and packet-level activity were not monitored. These observations are not a formal proof of absence of all side effects.

See application documentation for query, output and safety limits. Windows may perform internal activity while servicing read-only API calls.
