#include "core.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <winsvc.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

namespace ddbe {
namespace {
constexpr DWORD max_property_bytes = 1024 * 1024;
class DeviceSet {
  public:
    explicit DeviceSet(HDEVINFO h) : handle(h) {
        if (h == INVALID_HANDLE_VALUE)
            throw Failure("Cannot open the device information set", GetLastError(), "win32", 4);
    }
    ~DeviceSet() {
        SetupDiDestroyDeviceInfoList(handle);
    }
    DeviceSet(const DeviceSet &) = delete;
    DeviceSet &operator=(const DeviceSet &) = delete;
    HDEVINFO handle;
};
struct ServiceCloser {
    void operator()(SC_HANDLE h) const noexcept {
        if (h)
            CloseServiceHandle(h);
    }
};
using ServiceHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceCloser>;
Property malformed(const char *reason) {
    return absent(Availability::unavailable, reason);
}
std::wstring decode_string(const std::vector<BYTE> &data) {
    if (data.empty() || data.size() % sizeof(wchar_t) != 0)
        throw std::runtime_error("Invalid string size");
    std::vector<wchar_t> w(data.size() / sizeof(wchar_t));
    std::memcpy(w.data(), data.data(), data.size());
    if (w.back() != L'\0')
        throw std::runtime_error("Unterminated string");
    const auto end = std::find(w.begin(), w.end(), L'\0');
    if (!std::all_of(end, w.end(), [](wchar_t c) { return c == L'\0'; }))
        throw std::runtime_error("Embedded string terminator");
    return std::wstring(w.begin(), end);
}
Property get_property(HDEVINFO set, SP_DEVINFO_DATA &dev, const DEVPROPKEY &key, DEVPROPTYPE expected) {
    DWORD needed = 0;
    DEVPROPTYPE type = 0;
    if (!SetupDiGetDevicePropertyW(set, &dev, &key, &type, nullptr, 0, &needed, 0)) {
        const DWORD error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return windows_error(error);
    }
    for (unsigned retry = 0; retry < 4; ++retry) {
        if (needed == 0 || needed > max_property_bytes)
            return malformed("Property size is invalid or exceeds the 1 MiB safety limit.");
        std::vector<BYTE> data(needed);
        if (!SetupDiGetDevicePropertyW(set, &dev, &key, &type, data.data(), static_cast<DWORD>(data.size()),
                                       &needed, 0)) {
            const DWORD error = GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER)
                continue;
            return windows_error(error);
        }
        if (needed > data.size())
            return malformed("Property size changed unexpectedly.");
        data.resize(needed);
        if (type != expected)
            return malformed("Property has an unexpected type.");
        try {
            if (type == DEVPROP_TYPE_STRING)
                return known(utf8(decode_string(data)));
            if (type == DEVPROP_TYPE_STRING_LIST) {
                if (data.size() % sizeof(wchar_t) != 0)
                    return malformed("Invalid MULTI_SZ byte count.");
                std::vector<wchar_t> w(data.size() / sizeof(wchar_t));
                std::memcpy(w.data(), data.data(), data.size());
                const auto parsed = parse_multi_sz(w);
                std::vector<std::string> ids;
                ids.reserve(parsed.size());
                std::transform(parsed.begin(), parsed.end(), std::back_inserter(ids), utf8);
                return known(ids);
            }
            if (type == DEVPROP_TYPE_UINT32 && data.size() == sizeof(std::uint32_t)) {
                std::uint32_t v = 0;
                std::memcpy(&v, data.data(), sizeof(v));
                return known(v);
            }
            if (type == DEVPROP_TYPE_BOOLEAN && data.size() == sizeof(DEVPROP_BOOLEAN))
                return known(data[0] != DEVPROP_FALSE);
            if (type == DEVPROP_TYPE_FILETIME && data.size() == sizeof(std::uint64_t)) {
                std::uint64_t v = 0;
                std::memcpy(&v, data.data(), sizeof(v));
                return known(format_date(v));
            }
            if (type == DEVPROP_TYPE_GUID && data.size() == sizeof(GUID)) {
                GUID g{};
                std::memcpy(&g, data.data(), sizeof(g));
                wchar_t buffer[40]{};
                // Format locally; no COM initialization or dynamic API is needed.
                const int result = swprintf_s(buffer, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                                              g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2],
                                              g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
                if (result < 0)
                    return malformed("GUID formatting failed.");
                return known(utf8(buffer));
            }
        } catch (const std::runtime_error &) {
            return malformed("Property contains malformed text or date data.");
        }
        return malformed("Property has an invalid size.");
    }
    return malformed("Property changed repeatedly during query.");
}
Property cm_error(CONFIGRET c) {
    Availability s = Availability::unavailable;
    if (c == CR_ACCESS_DENIED)
        s = Availability::access_denied;
    if (c == CR_CALL_NOT_IMPLEMENTED)
        s = Availability::unsupported;
    auto p = absent(s, "Configuration Manager could not supply this metadata.");
    p.error = c;
    p.error_domain = "configret";
    return p;
}
Property instance_id(HDEVINFO set, SP_DEVINFO_DATA &dev) {
    DWORD needed = 0;
    if (SetupDiGetDeviceInstanceIdW(set, &dev, nullptr, 0, &needed))
        return malformed("Unexpected empty instance ID query success.");
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER)
        return windows_error(error);
    for (unsigned retry = 0; retry < 4; ++retry) {
        if (needed == 0 || needed > 32768)
            return malformed("Invalid instance ID length.");
        std::vector<wchar_t> data(needed);
        if (SetupDiGetDeviceInstanceIdW(set, &dev, data.data(), static_cast<DWORD>(data.size()), &needed)) {
            if (needed == 0 || needed > data.size() || data[needed - 1] != L'\0')
                return malformed("Malformed instance ID.");
            return known(utf8(std::wstring(data.data(), needed - 1)));
        }
        error = GetLastError();
        if (error != ERROR_INSUFFICIENT_BUFFER)
            return windows_error(error);
    }
    return malformed("Instance ID changed repeatedly during query.");
}
std::optional<std::string> string_value(const Property &p) {
    if (p.value) {
        if (const auto *s = std::get_if<std::string>(&*p.value))
            return *s;
    }
    return std::nullopt;
}
Device summary(HDEVINFO set, SP_DEVINFO_DATA &dev) {
    Device d;
    d.device["instance_id"] = instance_id(set, dev);
    d.device["friendly_name"] = get_property(set, dev, DEVPKEY_Device_FriendlyName, DEVPROP_TYPE_STRING);
    d.device["description"] = get_property(set, dev, DEVPKEY_Device_DeviceDesc, DEVPROP_TYPE_STRING);
    d.device["display_name"] =
        string_value(d.device["friendly_name"]) ? d.device["friendly_name"] : d.device["description"];
    d.device["class"] = get_property(set, dev, DEVPKEY_Device_Class, DEVPROP_TYPE_STRING);
    d.device["present"] = get_property(set, dev, DEVPKEY_Device_IsPresent, DEVPROP_TYPE_BOOLEAN);
    ULONG status = 0, problem = 0;
    const CONFIGRET result = CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0);
    if (result == CR_SUCCESS) {
        d.device["status_flags"] = known(static_cast<std::uint32_t>(status));
        d.device["status_names"] = known(status_names(status));
        d.device["problem_code"] = known(static_cast<std::uint32_t>(problem));
        d.device["problem_description"] = known(problem_name(problem));
    } else {
        for (const char *key : {"status_flags", "status_names", "problem_code", "problem_description"})
            d.device[key] = cm_error(result);
    }
    return d;
}
Property parent_id(DEVINST dev) {
    DEVINST parent = 0;
    CONFIGRET r = CM_Get_Parent(&parent, dev, 0);
    if (r != CR_SUCCESS)
        return cm_error(r); // A missing parent is not proof that this is the root.
    for (unsigned retry = 0; retry < 4; ++retry) {
        ULONG len = 0;
        r = CM_Get_Device_ID_Size(&len, parent, 0);
        if (r != CR_SUCCESS)
            return cm_error(r);
        if (len > 32767)
            return malformed("Invalid parent instance ID size.");
        std::vector<wchar_t> id(static_cast<std::size_t>(len) + 1);
        r = CM_Get_Device_IDW(parent, id.data(), static_cast<ULONG>(id.size()), 0);
        if (r == CR_BUFFER_SMALL)
            continue;
        if (r != CR_SUCCESS)
            return cm_error(r);
        return known(utf8(std::wstring(id.data())));
    }
    return malformed("Parent changed repeatedly during query.");
}
void service_config(Device &d) {
    const char *keys[] = {"service_image_path", "service_type", "service_start_type", "service_error_control",
                          "service_load_order_group"};
    const auto service = string_value(d.driver.at("service_name"));
    if (!service || service->empty()) {
        for (const char *key : keys)
            d.driver[key] =
                service ? absent(Availability::not_applicable, "The device has a known-empty service name.")
                        : absent(d.driver.at("service_name").state,
                                 "Service name is unavailable; service configuration cannot be queried.");
        return;
    }
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager) {
        const auto p = windows_error(GetLastError());
        for (const char *key : keys)
            d.driver[key] = p;
        return;
    }
    const auto wide = utf16(*service);
    ServiceHandle handle(OpenServiceW(manager.get(), wide.c_str(), SERVICE_QUERY_CONFIG));
    if (!handle) {
        const auto p = windows_error(GetLastError());
        for (const char *key : keys)
            d.driver[key] = p;
        return;
    }
    DWORD needed = 0;
    if (QueryServiceConfigW(handle.get(), nullptr, 0, &needed)) {
        for (const char *key : keys)
            d.driver[key] = malformed("Unexpected empty service configuration query success.");
        return;
    }
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) {
        const auto p = windows_error(error);
        for (const char *key : keys)
            d.driver[key] = p;
        return;
    }
    for (unsigned retry = 0; retry < 4; ++retry) {
        if (needed < sizeof(QUERY_SERVICE_CONFIGW) || needed > max_property_bytes)
            break;
        // Aligned allocation for the variable-length configuration and embedded strings.
        const std::size_t units =
            (static_cast<std::size_t>(needed) + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
        std::vector<std::max_align_t> storage(units);
        auto *config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(storage.data());
        if (!QueryServiceConfigW(handle.get(), config, needed, &needed)) {
            error = GetLastError();
            if (error == ERROR_INSUFFICIENT_BUFFER)
                continue;
            const auto p = windows_error(error);
            for (const char *key : keys)
                d.driver[key] = p;
            return;
        }
        d.driver["service_image_path"] = config->lpBinaryPathName
                                             ? known(utf8(config->lpBinaryPathName))
                                             : malformed("Service image path was not supplied.");
        d.driver["service_load_order_group"] = config->lpLoadOrderGroup
                                                   ? known(utf8(config->lpLoadOrderGroup))
                                                   : malformed("Service load order group was not supplied.");
        d.driver["service_type"] = known(static_cast<std::uint32_t>(config->dwServiceType));
        d.driver["service_start_type"] = known(static_cast<std::uint32_t>(config->dwStartType));
        d.driver["service_error_control"] = known(static_cast<std::uint32_t>(config->dwErrorControl));
        return;
    }
    for (const char *key : keys)
        d.driver[key] = malformed("Service configuration size is invalid or changed repeatedly.");
}
std::wstring lower(const std::wstring &s) {
    if (s.empty())
        return s;
    if (s.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        throw Failure("Filter text is too large", 0, "application", 4);
    const int length = static_cast<int>(s.size());
    const int required = LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, s.data(), length, nullptr, 0);
    if (required == 0)
        throw Failure("Cannot normalize filter text", GetLastError(), "win32", 4);
    std::wstring out(static_cast<std::size_t>(required), L'\0');
    if (LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, s.data(), length, out.data(), required) != required)
        throw Failure("Cannot normalize filter text", GetLastError(), "win32", 4);
    return out;
}
} // namespace
Failure device_open_failure(std::uint32_t error) {
    const bool missing =
        error == ERROR_NO_SUCH_DEVINST || error == ERROR_NOT_FOUND || error == ERROR_FILE_NOT_FOUND;
    return Failure(missing ? "Device instance was not found" : "Device instance could not be opened", error,
                   "win32", missing ? 3 : 4);
}
Device inspect_device(const std::wstring &id) {
    if (!valid_instance_id(id))
        throw Failure("Malformed device instance ID", 0, "cli", 2);
    DeviceSet set(SetupDiCreateDeviceInfoList(nullptr, nullptr));
    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    if (!SetupDiOpenDeviceInfoW(set.handle, id.c_str(), nullptr, 0, &dev)) {
        throw device_open_failure(GetLastError());
    }
    Device d = summary(set.handle, dev);
    d.device["manufacturer"] =
        get_property(set.handle, dev, DEVPKEY_Device_Manufacturer, DEVPROP_TYPE_STRING);
    d.device["class_guid"] = get_property(set.handle, dev, DEVPKEY_Device_ClassGuid, DEVPROP_TYPE_GUID);
    d.device["enumerator"] =
        get_property(set.handle, dev, DEVPKEY_Device_EnumeratorName, DEVPROP_TYPE_STRING);
    d.device["parent_instance_id"] = parent_id(dev.DevInst);
    d.identifiers["hardware_ids"] =
        get_property(set.handle, dev, DEVPKEY_Device_HardwareIds, DEVPROP_TYPE_STRING_LIST);
    d.identifiers["compatible_ids"] =
        get_property(set.handle, dev, DEVPKEY_Device_CompatibleIds, DEVPROP_TYPE_STRING_LIST);
    struct StringKey {
        const char *name;
        const DEVPROPKEY *key;
    };
    const StringKey props[] = {{"driver_key", &DEVPKEY_Device_Driver},
                               {"inf_name", &DEVPKEY_Device_DriverInfPath},
                               {"inf_section", &DEVPKEY_Device_DriverInfSection},
                               {"provider", &DEVPKEY_Device_DriverProvider},
                               {"version", &DEVPKEY_Device_DriverVersion},
                               {"matching_device_id", &DEVPKEY_Device_MatchingDeviceId},
                               {"service_name", &DEVPKEY_Device_Service}};
    for (const auto &p : props)
        d.driver[p.name] = get_property(set.handle, dev, *p.key, DEVPROP_TYPE_STRING);
    d.driver["date"] = get_property(set.handle, dev, DEVPKEY_Device_DriverDate, DEVPROP_TYPE_FILETIME);
    d.driver["configured_rank"] =
        get_property(set.handle, dev, DEVPKEY_Device_DriverRank, DEVPROP_TYPE_UINT32);
    d.driver["signer_trust"] =
        absent(Availability::unsupported,
               "Signature and trust verification are not implemented; provider is not a signer assertion.");
    service_config(d);
    return d;
}
Listing list_devices(const Options &o) {
    DeviceSet set(
        SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | (o.all ? 0UL : DIGCF_PRESENT)));
    Listing l;
    const auto match = lower(o.match);
    const auto class_filter = lower(o.class_filter);
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA dev{};
        dev.cbSize = sizeof(dev);
        if (!SetupDiEnumDeviceInfo(set.handle, index, &dev)) {
            const DWORD e = GetLastError();
            if (e == ERROR_NO_MORE_ITEMS)
                break;
            throw Failure("Device enumeration failed", e, "win32", 4);
        }
        Device d = summary(set.handle, dev);
        const auto id = string_value(d.device.at("instance_id"));
        const auto name = string_value(d.device.at("display_name"));
        const auto cls = string_value(d.device.at("class"));
        if (!class_filter.empty() && (!cls || lower(utf16(*cls)) != class_filter))
            continue;
        if (!match.empty() && (!id || lower(utf16(*id)).find(match) == std::wstring::npos) &&
            (!name || lower(utf16(*name)).find(match) == std::wstring::npos))
            continue;
        l.devices.push_back(std::move(d));
    }
    std::sort(l.devices.begin(), l.devices.end(), [](const Device &a, const Device &b) {
        return string_value(a.device.at("instance_id")).value_or("") <
               string_value(b.device.at("instance_id")).value_or("");
    });
    l.matched = l.devices.size();
    if (o.limit && l.devices.size() > o.limit)
        l.devices.resize(o.limit);
    return l;
}
} // namespace ddbe
