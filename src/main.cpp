#include "core.h"
#include <algorithm>
#include <exception>

int wmain(int argc, wchar_t *argv[]) {
    bool json = false;
    try {
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; ++i) {
            args.emplace_back(argv[i]);
            if (args.back() == L"--json")
                json = true;
        }
        const auto options = ddbe::parse_options(args);
        switch (options.mode) {
        case ddbe::Options::Mode::help:
            ddbe::write_output(options.json ? "{\"schema_version\":1,\"application_version\":\"1.0.0\",\"help\":" +
                                                  ddbe::json_escape(ddbe::help_text()) + "}\n"
                                            : ddbe::help_text());
            break;
        case ddbe::Options::Mode::version:
            ddbe::write_output(options.json ? "{\"schema_version\":1,\"application_version\":\"1.0.0\"}\n"
                                            : "device-driver-binding-explainer 1.0.0\n");
            break;
        case ddbe::Options::Mode::list:
            ddbe::write_output(ddbe::render_listing(ddbe::list_devices(options), options.json));
            break;
        case ddbe::Options::Mode::device:
            ddbe::write_output(ddbe::render_device(ddbe::inspect_device(options.instance_id), options.json));
            break;
        }
        return 0;
    } catch (const ddbe::Failure &e) {
        try {
            ddbe::write_output(json ? ddbe::render_error(e.what(), e.domain, e.code)
                                    : std::string("Error: ") + e.what() + " (" + e.domain + " " +
                                          std::to_string(e.code) + ")\n",
                               !json);
        } catch (const std::exception &) {
        }
        return e.exit_code;
    } catch (const std::exception &e) {
        try {
            ddbe::write_output(json ? ddbe::render_error(e.what(), "application", 0)
                                    : std::string("Error: ") + e.what() + '\n',
                               !json);
        } catch (const std::exception &) {
        }
        return 4;
    }
}
