#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <chrono>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "app/EngineLoop.h"
#include "app/ServeApp.h"
#include "cli/NetworkTableFormatter.h"
#include "cli/Options.h"
#include "cli/TableFormatter.h"
#include "network/Serializer.h"
#include "platform/windows/EmbeddedAssets.h"
#include "platform/windows/EtwNetworkCollector.h"
#include "platform/windows/EtwSchedulerCollector.h"
#include "platform/windows/WindowsConnectionScanner.h"
#include "platform/windows/WindowsSystemReader.h"

namespace {

// exe 가 있는 디렉터리 옆의 web 폴더 (릴리스 zip 의 구조).
std::filesystem::path webFolderNextToExe() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buffer).parent_path() / "web";
}

// 서버가 뜬 뒤 기본 브라우저로 화면을 연다 (인자 없이 실행했을 때만).
void openBrowser(unsigned short port) {
    const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
    // ShellExecute 는 COM 이 초기화된 스레드에서 부르라고 안내된다.
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (SUCCEEDED(com)) {
        ::CoUninitialize();
    }
}

// 콘솔이 곧바로 닫히는 더블클릭 실행에서는 실패 이유를 볼 수 없다. 대화상자로 한 번 알린다.
void showFailure(const std::string& message) {
    const int wide = ::MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    std::wstring text(static_cast<size_t>(wide > 0 ? wide : 1), L'\0');
    if (wide > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), wide);
    }
    ::MessageBoxW(nullptr, text.c_str(), L"Pulse Universe", MB_OK | MB_ICONERROR);
}

// Ctrl+C·창 닫기·로그오프로 죽으면 소멸자가 돌지 못해 커널 세션이 남는다. 계속 이벤트를
// 쌓는 세션을 두지 않도록 여기서 이름으로 멈춘다. FALSE 를 돌려 기본 종료 동작은 그대로 둔다.
// --serve 의 asio signal_set 이 Ctrl+C 를 먼저 가로채면 이 핸들러는 불리지 않고, 그
// 경로는 정상 종료하며 소멸자가 세션을 멈춘다.
BOOL WINAPI onConsoleControl(DWORD) {
    pulse::EtwSchedulerCollector::stopSessionByName();
    return FALSE;
}

// --connections 가 네트워크 ETW 세션을 열었을 때만 등록한다. 같은 이유로 이름으로 멈춘다.
BOOL WINAPI onNetworkConsoleControl(DWORD) {
    pulse::EtwNetworkCollector::stopSessionByName();
    return FALSE;
}

int runDump(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::EngineLoop loop(reader, cfg, [](const pulse::SystemSnapshot& snapshot) {
        std::printf("%s\n", pulse::formatSnapshotTable(snapshot).c_str());
        std::fflush(stdout);
    });
    loop.run();

    if (!loop.error().empty()) {
        std::printf("sampling failed: %s\n", loop.error().c_str());
        return 1;
    }
    return 0;
}

// M10 스펙 5절. 이 PC 의 연결을 프로세스별로 간격마다 출력한다. 관리자 권한이 필요 없다.
int runConnections(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::WindowsConnectionScanner scanner;

    // M11 스펙 D114: --mapping 이 트래픽 측정에도 같은 규칙으로 쓰인다.
    std::unique_ptr<pulse::EtwNetworkCollector> traffic;
    std::string traffic_note;
    if (options.mapping == pulse::Mapping::Estimated) {
        traffic_note = "--mapping estimated";
    } else {
        std::string error;
        traffic = pulse::EtwNetworkCollector::start(error);
        if (traffic != nullptr) {
            ::SetConsoleCtrlHandler(onNetworkConsoleControl, TRUE);
            std::fprintf(stderr, "network traffic: measured (ETW)\n");
        } else if (options.mapping == pulse::Mapping::Measured) {
            std::fprintf(stderr, "network traffic: cannot measure - %s\n", error.c_str());
            return 1;
        } else {
            traffic_note = error;
        }
    }
    // 공급자를 켠 직후 잠시는 이벤트가 오지 않는다. 기다린 뒤 첫 drain 으로 창을 연다 (첫 drain 은 시작일 뿐이다).
    if (traffic != nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        traffic->drain();
    }

    for (unsigned round = 0; options.iterations == 0 || round < options.iterations; ++round) {
        // 측정 중이면 첫 줄도 한 창을 채운 뒤에 낸다.
        if (round > 0 || traffic != nullptr) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
        std::optional<pulse::RawNetworkTraffic> window;
        if (traffic != nullptr) {
            window = traffic->drain();
            if (!window.has_value()) {
                traffic_note = "ETW stopped";
            }
        }
        const pulse::RawSample sample = reader.read();
        std::unordered_map<uint32_t, std::string> names;
        for (const pulse::RawProcess& process : sample.processes) {
            names[process.pid] = process.name;
        }
        const pulse::ConnectionScan scan = scanner.scan();
        if (!scan.error.empty()) {
            std::fprintf(stderr, "connections: %s\n", scan.error.c_str());
        }
        const pulse::NetworkView view = pulse::aggregateNetwork(scan.connections, names, window);
        std::printf("%s\n", pulse::formatNetworkTable(view, traffic_note).c_str());
        std::fflush(stdout);
    }
    if (traffic != nullptr) {
        // 수집기가 사라진 뒤에 Ctrl+C 가 다른 엔진의 세션을 멈추지 않게 핸들러를 뺀다.
        ::SetConsoleCtrlHandler(onNetworkConsoleControl, FALSE);
    }
    return 0;
}

int runJson(pulse::ISystemReader& reader, const pulse::Options& options) {
    pulse::EngineLoopConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = 2;  // 첫 스냅샷은 CPU 델타가 없어 버린다 — 계약서 6.1 절
    cfg.aggregator.filter.max_groups = options.max_groups;

    pulse::SystemSnapshot latest;
    pulse::EngineLoop loop(reader, cfg,
                           [&](const pulse::SystemSnapshot& s) { latest = s; });
    loop.run();

    if (!loop.error().empty()) {
        std::fprintf(stderr, "sampling failed: %s\n", loop.error().c_str());
        return 1;
    }

    std::printf("%s\n", pulse::serializeSnapshot(latest).c_str());
    return 0;
}

int runServe(pulse::ISystemReader& reader, const pulse::Options& options, bool open_browser) {
    pulse::ServeConfig cfg;
    cfg.interval_ms = options.interval_ms;
    cfg.iterations = options.iterations;
    cfg.max_groups = options.max_groups;
    cfg.server.port = static_cast<unsigned short>(options.port);
    if (!options.allowed_origins.empty()) {
        for (const auto& origin : options.allowed_origins) {
            cfg.server.allowed_origins.push_back(origin);
        }
    }
    if (!options.web_root.empty()) {
        std::error_code dir_ec;
        if (!std::filesystem::is_directory(options.web_root, dir_ec) || dir_ec) {
            std::fprintf(stderr, "web root is not a directory: %s\n",
                         options.web_root.c_str());
            return 2;
        }
        cfg.server.web_root = options.web_root;
    }
    if (options.use_embedded_web) {
        const pulse::AssetPack* assets = pulse::embeddedAssetPack();
        if (assets == nullptr) {
            std::fprintf(stderr, "no embedded web assets in this build\n");
            return 2;
        }
        cfg.server.assets = assets;
    }

    // M12 스펙 D125·D126. 연결 목록은 항상 모으고(관리자 권한 불필요), 트래픽 ETW 는 --mapping 규칙을 따른다.
    pulse::WindowsConnectionScanner scanner;
    std::unique_ptr<pulse::EtwNetworkCollector> traffic;
    if (options.mapping != pulse::Mapping::Estimated) {
        std::string error;
        traffic = pulse::EtwNetworkCollector::start(error);
        if (traffic != nullptr) {
            ::SetConsoleCtrlHandler(onNetworkConsoleControl, TRUE);
            std::fprintf(stderr, "network traffic: measured (ETW)\n");
            if (options.interval_ms < 300) {
                std::fprintf(stderr,
                             "network traffic: each cycle spends about 250 ms flushing ETW buffers, so the real "
                             "period will be longer than --interval-ms %u\n",
                             options.interval_ms);
            }
            // 공급자를 켠 직후 잠시는 이벤트가 오지 않는다. 첫 창이 비지 않게 기다린 뒤 서버를 시작한다.
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } else if (options.mapping == pulse::Mapping::Measured) {
            std::fprintf(stderr, "network traffic: cannot measure - %s\n", error.c_str());
            return 1;
        } else {
            std::fprintf(stderr, "network traffic: not measured - %s\n", error.c_str());
        }
    }
    cfg.network.scanner = &scanner;
    cfg.network.traffic = traffic.get();

    const bool has_web_root = !cfg.server.web_root.empty() || cfg.server.assets != nullptr;
    const pulse::ServeResult result =
        pulse::runServe(reader, cfg, [has_web_root, open_browser](unsigned short port) {
            std::printf("pulse-engine listening on ws://127.0.0.1:%u\n",
                        static_cast<unsigned>(port));
            if (has_web_root) {
                std::printf("open http://127.0.0.1:%u/ in a browser\n",
                            static_cast<unsigned>(port));
            }
            std::fflush(stdout);
            if (open_browser) {
                openBrowser(port);
            }
        });

    if (traffic != nullptr) {
        // 수집기가 사라진 뒤에 Ctrl+C 가 다른 엔진의 세션을 멈추지 않게 핸들러를 뺀다.
        ::SetConsoleCtrlHandler(onNetworkConsoleControl, FALSE);
    }
    if (!result.message.empty()) {
        std::fprintf(stderr, "%s\n", result.message.c_str());
        if (open_browser && result.exit_code != 0) {
            showFailure(result.message);
        }
    }
    return result.exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    pulse::Options options;
    std::string error;

    // 릴리스 zip 설계 D70, 웹 내장 설계 D77. 인자 없이 실행(더블클릭)하면 exe 옆의 web/ 을,
    // 없으면 내장본을 서빙하고 브라우저를 연다. 둘 다 없으면 지금처럼 사용법을 출력한다.
    bool launched_by_default = false;
    if (argc == 1) {
        const std::filesystem::path web = webFolderNextToExe();
        std::string web_string;
        bool exists = false;
        // path::string() 은 현재 코드 페이지로 바꿀 수 없는 경로에서 예외를 던진다. 그 경우
        // 폴더는 없는 것으로 보고 내장본으로 물러난다.
        try {
            std::error_code ec;
            exists = !web.empty() && std::filesystem::is_directory(web, ec) && !ec;
            web_string = web.string();
        } catch (const std::exception&) {
            exists = false;
            web_string.clear();
        }
        launched_by_default = pulse::applyDefaultLaunch(options, web_string, exists,
                                                        pulse::embeddedAssetPack() != nullptr);
    }

    switch (launched_by_default ? pulse::ParseResult::Ok
                                : pulse::parseOptions(argc, argv, options, error)) {
        case pulse::ParseResult::Ok:
            break;
        case pulse::ParseResult::ShowUsage:
            std::printf("%s", pulse::usageText().c_str());
            return 0;
        case pulse::ParseResult::Error:
            std::printf("%s\n\n%s", error.c_str(), pulse::usageText().c_str());
            return 2;
    }

    // ETW 스펙 8절. auto 와 measured 는 실측을 시도한다. 결과는 stderr 에 한 줄 남긴다 —
    // --json 의 stdout 을 더럽히지 않는다.
    // --connections 는 스레드 매핑이 필요 없으니 ETW 를 켜지 않는다.
    const bool want_measured =
        options.mapping != pulse::Mapping::Estimated && options.mode != pulse::Mode::Connections;
    pulse::WindowsSystemReader reader(want_measured);
    if (want_measured) {
        if (reader.measuringThreads()) {
            ::SetConsoleCtrlHandler(onConsoleControl, TRUE);
            std::fprintf(stderr, "thread mapping: measured (ETW)\n");
        } else if (options.mapping == pulse::Mapping::Measured) {
            std::fprintf(stderr, "thread mapping: cannot measure - %s\n",
                         reader.mappingError().c_str());
            return 1;
        } else {
            std::fprintf(stderr, "thread mapping: estimated - %s\n",
                         reader.mappingError().c_str());
        }
    }

    switch (options.mode) {
        case pulse::Mode::Dump:
            return runDump(reader, options);
        case pulse::Mode::Json:
            return runJson(reader, options);
        case pulse::Mode::Serve:
            return runServe(reader, options, launched_by_default);
        case pulse::Mode::Connections:
            return runConnections(reader, options);
        case pulse::Mode::None:
            break;
    }
    return 0;
}
