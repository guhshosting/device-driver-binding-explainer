#include "core.h"
#include <algorithm>
#include <cfg.h>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace ddbe {
Failure::Failure(std::string message, std::uint32_t c, std::string d, int e)
    : code(c), domain(std::move(d)), exit_code(e), message_(std::move(message)) {}
const char *Failure::what() const noexcept {
    return message_.c_str();
}
Property known(Value v) {
    Property p;
    p.state = Availability::available;
    p.value = std::move(v);
    return p;
}
Property absent(Availability s, std::string reason) {
    Property p;
    p.state = s;
    p.reason = std::move(reason);
    return p;
}
Property windows_error(std::uint32_t e) {
    Availability s = Availability::unavailable;
    if (e == ERROR_ACCESS_DENIED)
        s = Availability::access_denied;
    if (e == ERROR_NOT_SUPPORTED || e == ERROR_CALL_NOT_IMPLEMENTED || e == ERROR_PROC_NOT_FOUND)
        s = Availability::unsupported;
    auto p = absent(s, e == ERROR_NOT_FOUND || e == ERROR_FILE_NOT_FOUND
                           ? "Metadata was not supplied or could not be found."
                           : "The read-only query failed.");
    p.error = e;
    p.error_domain = "win32";
    return p;
}
std::string availability_name(Availability s) {
    switch (s) {
    case Availability::available:
        return "available";
    case Availability::unavailable:
        return "unavailable";
    case Availability::access_denied:
        return "access_denied";
    case Availability::unsupported:
        return "unsupported";
    case Availability::not_applicable:
        return "not_applicable";
    }
    throw std::logic_error("Invalid availability");
}
std::string utf8(const std::wstring &s) {
    if (s.empty())
        return {};
    if (s.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("Text too large");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (!n)
        throw std::runtime_error("Invalid UTF-16 metadata");
    std::string out(static_cast<std::size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(),
                            n, nullptr, nullptr) != n)
        throw std::runtime_error("UTF-8 conversion failed");
    return out;
}
std::wstring utf16(const std::string &s) {
    if (s.empty())
        return {};
    if (s.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw std::runtime_error("Text too large");
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(),
                            n) != n)
        throw std::runtime_error("UTF-16 conversion failed");
    return out;
}
std::vector<std::wstring> parse_multi_sz(const std::vector<wchar_t> &data) {
    if (data.size() < 2 || data[data.size() - 1] != L'\0' || data[data.size() - 2] != L'\0')
        throw std::runtime_error("Malformed MULTI_SZ termination");
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start < data.size()) {
        auto end = std::find(data.begin() + static_cast<std::ptrdiff_t>(start), data.end(), L'\0');
        const auto pos = static_cast<std::size_t>(end - data.begin());
        if (pos == start) {
            if (!std::all_of(end, data.end(), [](wchar_t c) { return c == L'\0'; }))
                throw std::runtime_error("Data follows MULTI_SZ terminator");
            break;
        }
        result.emplace_back(data.data() + start, pos - start);
        start = pos + 1;
    }
    return result;
}
bool valid_instance_id(const std::wstring &id) {
    if (id.empty() || id.size() >= 200)
        return false;
    unsigned separators = 0;
    bool component = false;
    for (const wchar_t c : id) {
        if (c < 0x20 || c == 0x7f || c == L'/')
            return false;
        if (c == L'\\') {
            if (!component)
                return false;
            ++separators;
            component = false;
        } else
            component = true;
    }
    return separators == 2 && component;
}
Options parse_options(const std::vector<std::wstring> &args) {
    Options o;
    if (args.empty())
        return o;
    bool mode_set = false;
    bool list_option = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto &a = args[i];
        if (a == L"--json")
            o.json = true;
        else if (a == L"--help" || a == L"--version") {
            if (mode_set)
                throw Failure("Choose one command", 0, "cli", 2);
            o.mode = a == L"--help" ? Options::Mode::help : Options::Mode::version;
            mode_set = true;
        } else if (a == L"list" || a == L"device") {
            if (mode_set)
                throw Failure("Choose one command", 0, "cli", 2);
            mode_set = true;
            o.mode = a == L"list" ? Options::Mode::list : Options::Mode::device;
            if (o.mode == Options::Mode::device) {
                if (++i == args.size() || !valid_instance_id(args[i]))
                    throw Failure("Expected canonical instance ID: enumerator\\device\\instance (maximum 199 "
                                  "characters)",
                                  0, "cli", 2);
                o.instance_id = args[i];
            }
        } else if (a == L"--all") {
            o.all = true;
            list_option = true;
        } else if (a == L"--match" || a == L"--class" || a == L"--limit") {
            list_option = true;
            if (++i == args.size())
                throw Failure("Option requires a value", 0, "cli", 2);
            if (a == L"--match")
                o.match = args[i];
            else if (a == L"--class")
                o.class_filter = args[i];
            else {
                std::uint32_t n = 0;
                if (args[i].empty())
                    throw Failure("Invalid list limit", 0, "cli", 2);
                for (wchar_t c : args[i]) {
                    if (c < L'0' || c > L'9' || n > 100000)
                        throw Failure("List limit must be 0..1000000 (0 means unlimited)", 0, "cli", 2);
                    n = n * 10 + static_cast<std::uint32_t>(c - L'0');
                }
                if (n > 1000000)
                    throw Failure("List limit too large", 0, "cli", 2);
                o.limit = n;
            }
        } else
            throw Failure("Unknown argument: " + utf8(a), 0, "cli", 2);
    }
    if (o.mode != Options::Mode::list && list_option)
        throw Failure("List filters require the list command", 0, "cli", 2);
    return o;
}
std::string json_escape(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '"':
            out << "\\\"";
            break;
        case '\\':
            out << "\\\\";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (c < 32)
                out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c)
                    << std::dec;
            else
                out << static_cast<char>(c);
        }
    }
    out << '"';
    return out.str();
}
static std::string value_json(const Value &v) {
    if (const auto *s = std::get_if<std::string>(&v))
        return json_escape(*s);
    if (const auto *n = std::get_if<std::uint32_t>(&v))
        return std::to_string(*n);
    if (const auto *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    std::string out = "[";
    for (const auto &s : std::get<std::vector<std::string>>(v)) {
        if (out.size() > 1)
            out += ',';
        out += json_escape(s);
    }
    return out + ']';
}
std::string property_json(const Property &p) {
    std::string out = "{\"availability\":" + json_escape(availability_name(p.state)) +
                      ",\"value\":" + (p.value ? value_json(*p.value) : "null");
    out += ",\"error\":";
    out += p.error
               ? "{\"domain\":" + json_escape(p.error_domain) + ",\"code\":" + std::to_string(*p.error) + "}"
               : "null";
    return out + ",\"reason\":" + (p.reason.empty() ? "null" : json_escape(p.reason)) + "}";
}
static std::string fields_json(const Fields &fields) {
    std::string out = "{";
    for (const auto &p : fields) {
        if (out.size() > 1)
            out += ',';
        out += json_escape(p.first) + ':' + property_json(p.second);
    }
    return out + '}';
}
std::string device_json(const Device &d) {
    return "{\"device\":" + fields_json(d.device) + ",\"identifiers\":" + fields_json(d.identifiers) +
           ",\"driver_association\":" + fields_json(d.driver) + "}";
}
const std::vector<std::string> &explanations() {
    static const std::vector<std::string> notes = {
        "Observed properties describe current device configuration and metadata; reads are not an atomic "
        "snapshot.",
        "Windows' full candidate/ranking decision is not reconstructed. A configured matching ID or rank, "
        "when supplied, is evidence about the observed association, not a comparison of all eligible "
        "packages.",
        "The observed INF need not have been the only eligible package. Windows may choose differently after "
        "reinstall; version alone does not determine ranking.",
        "Service image path and start configuration do not establish that the driver is loaded, running, or "
        "functioning.",
        "No Configuration Manager problem does not prove hardware health. Unavailable metadata does not mean "
        "no driver exists.",
        "Signer and cryptographic trust are not evaluated. No files are downloaded and no device, driver, "
        "service, registry, or driver-store configuration is changed."};
    return notes;
}
static std::string notes_json() {
    return value_json(Value(explanations()));
}
static std::string field_text(const Property &p) {
    if (!p.value)
        return "[" + availability_name(p.state) + "] " + p.reason +
               (p.error ? " (" + p.error_domain + " " + std::to_string(*p.error) + ")" : "");
    if (const auto *s = std::get_if<std::string>(&*p.value)) {
        std::string out;
        for (unsigned char c : *s) {
            if (c < 32 || c == 127)
                out += '?';
            else
                out += static_cast<char>(c);
        }
        return out.empty() ? "(known empty)" : out;
    }
    return value_json(*p.value);
}
std::string render_device(const Device &d, bool json) {
    if (json)
        return "{\"schema_version\":1,\"application_version\":\"1.0.0\",\"mode\":\"device\",\"result\":" +
               device_json(d) + ",\"explanation\":" + notes_json() + "}\n";
    std::string out;
    for (const auto &group :
         {std::make_pair("DEVICE", &d.device), std::make_pair("IDENTIFIERS", &d.identifiers),
          std::make_pair("DRIVER ASSOCIATION", &d.driver)}) {
        out += std::string(group.first) + '\n';
        for (const auto &f : *group.second)
            out += "  " + f.first + ": " + field_text(f.second) + '\n';
        out += '\n';
    }
    out += "EXPLANATION\n";
    for (const auto &n : explanations())
        out += "  " + n + '\n';
    return out;
}
std::string render_listing(const Listing &l, bool json) {
    if (json) {
        std::string out =
            "{\"schema_version\":1,\"application_version\":\"1.0.0\",\"mode\":\"list\",\"matched_count\":" +
            std::to_string(l.matched) + ",\"shown_count\":" + std::to_string(l.devices.size()) +
            ",\"truncated\":" + (l.matched > l.devices.size() ? "true" : "false") + ",\"devices\":[";
        for (std::size_t i = 0; i < l.devices.size(); ++i) {
            if (i)
                out += ',';
            out += fields_json(l.devices[i].device);
        }
        return out + "]}\n";
    }
    std::string out = "INSTANCE ID | DISPLAY NAME | CLASS | PRESENT | CM PROBLEM | CM STATUS\n";
    for (const auto &d : l.devices) {
        bool first = true;
        for (const char *key :
             {"instance_id", "display_name", "class", "present", "problem_code", "status_flags"}) {
            if (!first)
                out += " | ";
            first = false;
            out += field_text(d.device.at(key));
        }
        out += '\n';
    }
    return out + "Shown " + std::to_string(l.devices.size()) + " of " + std::to_string(l.matched) +
           " matching devices. Use --limit 0 for all matches.\n";
}
std::string render_error(const std::string &m, const std::string &domain, std::uint32_t code) {
    return "{\"schema_version\":1,\"application_version\":\"1.0.0\",\"error\":{\"message\":" +
           json_escape(m) + ",\"domain\":" + json_escape(domain) + ",\"code\":" + std::to_string(code) +
           "}}\n";
}
std::string format_date(std::uint64_t ft) {
    FILETIME t{static_cast<DWORD>(ft), static_cast<DWORD>(ft >> 32)};
    SYSTEMTIME s{};
    if (!FileTimeToSystemTime(&t, &s))
        throw std::runtime_error("Invalid FILETIME");
    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << s.wYear << '-' << std::setw(2) << s.wMonth << '-'
        << std::setw(2) << s.wDay;
    return out.str();
}
std::string format_version(const std::string &v) {
    if (v.empty())
        throw std::runtime_error("Empty version metadata");
    // Preserve the provider's version string; do not infer precedence or ranking.
    return v;
}
std::string problem_name(std::uint32_t c) {
    static const char *const names[] = {
        "No Configuration Manager problem",
        "Not configured",
        "Device loader failed",
        "Out of memory",
        "Invalid device data",
        "Arbitration failed",
        "Boot configuration conflict",
        "Filtering failed",
        "Device loader not found",
        "Invalid data",
        "Device failed to start",
        "Liar",
        "Insufficient free resources",
        "Not verified",
        "Restart required (reported by Windows; this utility does not request a reboot)",
        "Re-enumeration problem",
        "Partial resource configuration",
        "Unknown resource type",
        "Reinstall requested by Windows",
        "Registry configuration problem",
        "VxD loader failure",
        "Device will be removed",
        "Device disabled",
        "Device loader not ready",
        "Device not present",
        "Move failed",
        "Too early",
        "No valid logical configuration",
        "Installation failed",
        "Hardware disabled",
        "Cannot share IRQ",
        "Driver addition failed",
        "Service disabled",
        "Cannot translate resources",
        "No configuration",
        "BIOS table problem",
        "IRQ translation failed",
        "Driver entry failed",
        "Driver failed prior unload",
        "Driver failed to load",
        "Service key invalid",
        "Legacy service has no devices",
        "Duplicate device",
        "Device reported failure",
        "Post-start failure",
        "Phantom device",
        "System shutdown",
        "Held for eject",
        "Driver blocked",
        "Registry too large",
        "Set properties failed",
        "Waiting on dependency",
        "Unsigned driver",
        "Device used by debugger",
        "Device reset",
        "Console locked",
        "Class configuration pending",
        "Assignment to guest partition failed"};
    if (c < sizeof(names) / sizeof(names[0]))
        return names[c];
    return "Unknown Configuration Manager problem (" + std::to_string(c) + ")";
}
std::vector<std::string> status_names(std::uint32_t s) {
    std::vector<std::string> names;
    // Use the NT meanings of aliased legacy bits on our Windows 10+ baseline.
    const std::pair<std::uint32_t, const char *> flags[] = {
        {DN_ROOT_ENUMERATED, "root_enumerated"},
        {DN_DRIVER_LOADED, "driver_loaded_flag"},
        {DN_ENUM_LOADED, "enumerator_loaded"},
        {DN_STARTED, "started"},
        {DN_MANUAL, "manual"},
        {DN_NEED_TO_ENUM, "need_to_enumerate"},
        {DN_DRIVER_BLOCKED, "driver_blocked"},
        {DN_HARDWARE_ENUM, "hardware_enumerated"},
        {DN_NEED_RESTART, "restart_needed_flag"},
        {DN_CHILD_WITH_INVALID_ID, "child_with_invalid_id"},
        {DN_HAS_PROBLEM, "has_problem"},
        {DN_FILTERED, "filtered"},
        {DN_LEGACY_DRIVER, "legacy_driver"},
        {DN_DISABLEABLE, "disableable"},
        {DN_REMOVABLE, "removable"},
        {DN_PRIVATE_PROBLEM, "private_problem"},
        {DN_QUERY_REMOVE_PENDING, "query_remove_pending"},
        {DN_QUERY_REMOVE_ACTIVE, "query_remove_active"},
        {DN_WILL_BE_REMOVED, "will_be_removed"},
        {DN_NT_ENUMERATOR, "nt_enumerator"},
        {DN_NT_DRIVER, "nt_driver"},
        {DN_DEVICE_DISCONNECTED, "device_disconnected"},
        {DN_NO_SHOW_IN_DM, "hidden_in_device_manager"}};
    std::uint32_t recognized = 0;
    for (const auto &f : flags) {
        recognized |= f.first;
        if ((s & f.first) != 0)
            names.emplace_back(f.second);
    }
    if ((s & ~recognized) != 0)
        names.push_back("other_flags:" + std::to_string(s & ~recognized));
    return names;
}
std::string help_text() {
    return "device-driver-binding-explainer 1.0.0\nRead-only Windows PnP device and configured driver "
           "metadata. No network access.\n\nUsage:\n  device-driver-binding-explainer list [--all] [--match "
           "text] [--class name] [--limit N] [--json]\n  device-driver-binding-explainer device "
           "<enumerator\\device\\instance> [--json]\n  device-driver-binding-explainer --help\n  "
           "device-driver-binding-explainer --version [--json]\n\nList defaults to present devices, at most "
           "100 matches, sorted by instance ID.\n--all includes non-present devices; --match searches "
           "ID/display name; --class\ncompares the class name. Filters are case-insensitive. --limit 0 is "
           "unlimited.\nQuote instance IDs in your shell. JSON is UTF-8 with schema_version 1.\nExit codes: "
           "0 success, 2 usage, 3 missing device, 4 query/output failure.\nPartial metadata remains a "
           "successful result with per-field availability.\nWindows' full driver candidate/ranking decision "
           "is not reconstructed.\n";
}
void write_output(const std::string &text, bool error) {
    HANDLE h = GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
        throw std::runtime_error("Output handle unavailable");
    if (GetConsoleMode(h, &mode)) {
        const auto w = utf16(text);
        std::size_t offset = 0;
        while (offset < w.size()) {
            DWORD done = 0;
            auto n = static_cast<DWORD>((std::min)(w.size() - offset, std::size_t{16000}));
            // Keep a UTF-16 surrogate pair together across console write chunks.
            if (offset + n < w.size() && w[offset + n - 1] >= 0xd800 && w[offset + n - 1] <= 0xdbff)
                --n;
            if (!WriteConsoleW(h, w.data() + offset, n, &done, nullptr) || done == 0)
                throw std::runtime_error("Console write failed");
            offset += done;
        }
    } else {
        std::size_t offset = 0;
        while (offset < text.size()) {
            DWORD done = 0;
            const auto n = static_cast<DWORD>((std::min)(text.size() - offset, std::size_t{65536}));
            if (!WriteFile(h, text.data() + offset, n, &done, nullptr) || done == 0)
                throw std::runtime_error("Output write failed");
            offset += done;
        }
    }
}
} // namespace ddbe
