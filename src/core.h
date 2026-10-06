#pragma once
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ddbe {
enum class Availability : std::uint8_t { available, unavailable, access_denied, unsupported, not_applicable };
using Value = std::variant<std::string, std::vector<std::string>, std::uint32_t, bool>;
struct Property {
    Availability state = Availability::unavailable;
    std::optional<Value> value;
    std::optional<std::uint32_t> error;
    std::string error_domain;
    std::string reason;
};
using Fields = std::map<std::string, Property>;
struct Device {
    Fields device;
    Fields identifiers;
    Fields driver;
};
struct Options {
    enum class Mode : std::uint8_t { help, version, list, device } mode = Mode::help;
    bool json = false;
    bool all = false;
    std::uint32_t limit = 100;
    std::wstring instance_id;
    std::wstring match;
    std::wstring class_filter;
};
struct Listing {
    std::vector<Device> devices;
    std::size_t matched = 0;
};
class Failure : public std::exception {
  public:
    Failure(std::string message, std::uint32_t code, std::string domain, int exit_code);
    const char *what() const noexcept override;
    std::uint32_t code;
    std::string domain;
    int exit_code;

  private:
    std::string message_;
};
Property known(Value value);
Property absent(Availability state, std::string reason);
Property windows_error(std::uint32_t error);
Failure device_open_failure(std::uint32_t error);
std::string availability_name(Availability state);
std::string utf8(const std::wstring &text);
std::wstring utf16(const std::string &text);
std::vector<std::wstring> parse_multi_sz(const std::vector<wchar_t> &data);
bool valid_instance_id(const std::wstring &id);
Options parse_options(const std::vector<std::wstring> &args);
std::string json_escape(const std::string &text);
std::string property_json(const Property &property);
std::string device_json(const Device &device);
std::string render_device(const Device &device, bool json);
std::string render_listing(const Listing &listing, bool json);
std::string render_error(const std::string &message, const std::string &domain, std::uint32_t code);
std::string format_date(std::uint64_t filetime);
std::string format_version(const std::string &version);
std::string problem_name(std::uint32_t code);
std::vector<std::string> status_names(std::uint32_t status);
const std::vector<std::string> &explanations();
Listing list_devices(const Options &options);
Device inspect_device(const std::wstring &id);
std::string help_text();
void write_output(const std::string &text, bool error = false);
} // namespace ddbe
