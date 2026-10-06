#include "core.h"
#include <cfg.h>
#include <iostream>
#include <stdexcept>
#include <windows.h>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, message);
}
void fixtures() {
    using namespace ddbe;
    require(parse_multi_sz({L'\0', L'\0'}).empty(), "Empty MULTI_SZ");
    require(parse_multi_sz({L'A', L'\0', L'B', L'\0', L'\0'}) == std::vector<std::wstring>{L"A", L"B"},
            "MULTI_SZ entries");
    require(parse_multi_sz({L'A', L'\0', L'\0', L'\0'}).size() == 1, "MULTI_SZ padding");
    rejects([] { parse_multi_sz({}); }, "Missing MULTI_SZ");
    rejects([] { parse_multi_sz({L'A', L'\0'}); }, "Single terminator");
    rejects([] { parse_multi_sz({L'A', L'\0', L'\0', L'B', L'\0', L'\0'}); }, "Embedded terminator");
    rejects([] { parse_multi_sz({L'A', L'B'}); }, "Unterminated MULTI_SZ");
    const wchar_t ids[] = L"PCI\\VEN_1234&DEV_5678\0PCI\\VEN_1234\0";
    const auto parsed = parse_multi_sz(std::vector<wchar_t>(std::begin(ids), std::end(ids)));
    require(parsed.size() == 2 && parsed[0] == L"PCI\\VEN_1234&DEV_5678", "Hardware IDs preserved");
    require(parsed[1] == L"PCI\\VEN_1234", "Compatible IDs preserved");
    require(valid_instance_id(L"PCI\\VEN_1234&DEV_5678\\1&ABC&0"), "Canonical ID accepted");
    for (const auto *bad : {L"", L"PCI", L"PCI\\ONE", L"\\ONE\\TWO", L"PCI\\\\ONE", L"PCI\\ONE\\",
                            L"PCI\\ONE\\TWO\\THREE", L"PCI/ONE/TWO", L"PCI\\ONE\\TWO\n"})
        require(!valid_instance_id(bad), "Malformed ID rejected");
    require(!valid_instance_id(L"PCI\\ONE\\" + std::wstring(200, L'A')), "Oversized ID rejected");
    require(utf8(utf16("Device \xE2\x98\x83")) == "Device \xE2\x98\x83", "Unicode round trip");
    rejects([] { utf8(std::wstring(1, static_cast<wchar_t>(0xd800))); }, "Unpaired UTF-16 rejected");
    rejects([] { utf16("\xff"); }, "Invalid UTF-8 rejected");
    require(format_version("1.2.003.4") == "1.2.003.4", "Version preserved");
    rejects([] { format_version(""); }, "Empty version invalid");
    SYSTEMTIME date{};
    date.wYear = 2024;
    date.wMonth = 2;
    date.wDay = 29;
    FILETIME ft{};
    require(SystemTimeToFileTime(&date, &ft) != FALSE, "Date fixture");
    require(format_date((static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) ==
                "2024-02-29",
            "Leap date formatting");
    require(problem_name(0) == "No Configuration Manager problem", "No problem");
    require(problem_name(22) == "Device disabled", "Known CM problem");
    require(problem_name(43) == "Device reported failure", "Failure CM problem");
    require(problem_name(57) == "Assignment to guest partition failed", "Guest assignment CM problem");
    require(problem_name(999).find("999") != std::string::npos, "Unknown CM problem numeric");
    const auto statuses = status_names(DN_STARTED | DN_HAS_PROBLEM);
    require(statuses == std::vector<std::string>{"started", "has_problem"}, "Status flags decode");
    require(status_names(0).empty(), "Zero flags");
    require(status_names(DN_DRIVER_BLOCKED) == std::vector<std::string>{"driver_blocked"},
            "Modern flag aliases");
    rejects([] { format_date(UINT64_MAX); }, "Invalid FILETIME rejected");
    require(!status_names(0x80000000).empty(), "Unknown flags retained");
    require(json_escape("a\"\\\n\t\x01") == "\"a\\\"\\\\\\n\\t\\u0001\"", "JSON escape");
    require(property_json(known(std::vector<std::string>{"PCI\\A", "PCI\\B"})) ==
                "{\"availability\":\"available\",\"value\":[\"PCI\\\\A\",\"PCI\\\\B\"],\"error\":null,"
                "\"reason\":null}",
            "JSON ID arrays");
    require(property_json(known(std::string{})).find("\"value\":\"\"") != std::string::npos,
            "Known empty string");
    require(property_json(known(std::vector<std::string>{})).find("\"value\":[]") != std::string::npos,
            "Known empty array");
    require(property_json(windows_error(ERROR_NOT_FOUND)).find("\"value\":null") != std::string::npos,
            "Unavailable is null");
    require(windows_error(ERROR_ACCESS_DENIED).state == Availability::access_denied, "Access denied");
    require(windows_error(ERROR_NOT_SUPPORTED).state == Availability::unsupported, "Unsupported");
    require(windows_error(ERROR_FILE_NOT_FOUND).state == Availability::unavailable, "Missing metadata");
    const auto missing_device = device_open_failure(ERROR_FILE_NOT_FOUND);
    require(missing_device.exit_code == 3 && missing_device.code == ERROR_FILE_NOT_FOUND &&
                missing_device.domain == "win32",
            "Missing device fixture preserves exit and error");
    require(device_open_failure(ERROR_ACCESS_DENIED).exit_code == 4,
            "Denied device open is not a missing device");
    require(availability_name(Availability::not_applicable) == "not_applicable", "Not applicable");
    Device d;
    d.device["instance_id"] = known(std::string("ROOT\\TEST\\1"));
    d.identifiers["hardware_ids"] = known(std::vector<std::string>{"ROOT\\TEST"});
    d.identifiers["compatible_ids"] = windows_error(ERROR_NOT_FOUND);
    d.driver["version"] = absent(Availability::access_denied, "Fixture denied");
    const auto rendered = render_device(d, true);
    require(rendered == render_device(d, true), "Deterministic JSON");
    require(rendered.find("\"schema_version\":1") != std::string::npos &&
                rendered.find("\"compatible_ids\":{\"availability\":\"unavailable\",\"value\":null") !=
                    std::string::npos,
            "Structured fixture");
    require(rendered.find("full candidate/ranking decision is not reconstructed") != std::string::npos,
            "Ranking limitation");
    Listing l;
    l.matched = 2;
    require(render_listing(l, true).find("\"truncated\":true") != std::string::npos, "List truncation");
    require(parse_options({L"list", L"--json", L"--limit", L"0", L"--all"}).limit == 0, "List options");
    require(parse_options({L"--json", L"device", L"ROOT\\TEST\\1"}).mode == Options::Mode::device,
            "Device options");
    rejects([] { parse_options({L"device", L"bad"}); }, "Malformed CLI ID");
    rejects([] { parse_options({L"list", L"--limit", L"-1"}); }, "Negative limit");
    rejects([] { parse_options({L"list", L"--limit", L"9999999999999"}); }, "Overflow limit");
    rejects([] { parse_options({L"list", L"--bogus"}); }, "Unknown option");
    rejects([] { parse_options({L"device", L"ROOT\\TEST\\1", L"--all"}); }, "Wrong mode filter");
    rejects([] { inspect_device(L"invalid"); }, "Backend malformed ID");
}
void live() {
    using namespace ddbe;
    Options o;
    o.mode = Options::Mode::list;
    o.limit = 0;
    const auto l = list_devices(o);
    require(!l.devices.empty(), "Live host must have ordinary PnP devices");
    std::size_t inspected = 0;
    for (const auto &summary : l.devices) {
        const auto &p = summary.device.at("instance_id");
        require(p.value.has_value(), "Live instance ID");
        const auto &id = std::get<std::string>(*p.value);
        const auto d = inspect_device(utf16(id));
        require(d.identifiers.count("hardware_ids") == 1 && d.identifiers.count("compatible_ids") == 1,
                "Live ID fields");
        require(d.driver.count("inf_name") == 1 && d.driver.count("service_image_path") == 1,
                "Live driver fields");
        require(render_device(d, true).find("\"schema_version\":1") != std::string::npos, "Live JSON");
        std::cout << "Inspected: " << id << '\n';
        ++inspected;
    }
    bool missing = false;
    try {
        inspect_device(L"ROOT\\DDBE_MISSING_7F624BEC_296A_4E39\\0000");
    } catch (const Failure &e) {
        missing = e.exit_code == 3;
    }
    require(missing, "Missing device gives exit 3");
    o.all = true;
    const auto all = list_devices(o);
    require(!all.devices.empty(), "All-device enumeration");
    std::cout << "Present snapshot: " << l.matched << "; all snapshot: " << all.matched
              << "; inspected: " << inspected << '\n';
}
} // namespace
int main(int argc, char *argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--live")
            live();
        else
            fixtures();
        std::cout << "All checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
