#include "app/Paths.h"
#include "app/Utf8.h"
#include "app/resource.h"
#include "bridge/AppBridge.h"

#include <dwmapi.h>
#include <windows.h>
#include <wrl.h>

#include <memory>
#include <string>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"EDITYHost";
constexpr int kMinWidth = 1100;
constexpr int kMinHeight = 720;

HWND g_hwnd = nullptr;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;
std::unique_ptr<edity::AppBridge> g_bridge;

void ResizeWebView() {
    if (!g_controller || !g_hwnd) {
        return;
    }
    RECT bounds{};
    GetClientRect(g_hwnd, &bounds);
    g_controller->put_Bounds(bounds);
}

void ApplyDarkFrame(HWND hwnd) {
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
    const COLORREF caption = RGB(11, 13, 16);
    const COLORREF border = RGB(232, 93, 4);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                ResizeWebView();
            }
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = kMinWidth;
            info->ptMinTrackSize.y = kMinHeight;
            return 0;
        }
        case edity::kPostToUi:
            edity::DeliverPostedJson(g_webview.Get(), lParam);
            return 0;
        case WM_DESTROY:
            g_bridge.reset();
            g_controller.Reset();
            g_webview.Reset();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

std::wstring UiUrl() {
    return L"https://edity.app/index.html";
}

void BindMessages() {
    g_webview->add_WebMessageReceived(
        Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (FAILED(args->get_WebMessageAsJson(&raw)) || !raw) {
                    return S_OK;
                }
                const std::string json = edity::WideToUtf8(raw);
                CoTaskMemFree(raw);
                if (g_bridge) {
                    g_bridge->HandleWebMessage(json);
                }
                return S_OK;
            })
            .Get(),
        nullptr);
}

HRESULT CreateWebView() {
    const auto userData = edity::AppDataDir() / L"webview";
    edity::EnsureDirectory(userData);
    const auto ui = edity::UiDir();
    if (!std::filesystem::exists(ui / "index.html")) {
        MessageBoxW(g_hwnd, L"HUD files are missing. ui/index.html was not found next to EDITY.exe.", L"EDITY",
                    MB_ICONERROR);
        return E_FAIL;
    }

    return CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userData.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [ui](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    MessageBoxW(g_hwnd,
                                L"WebView2 runtime is missing. Install the Evergreen runtime from Microsoft and try again.",
                                L"EDITY", MB_ICONERROR);
                    return result;
                }
                return env->CreateCoreWebView2Controller(
                    g_hwnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                                [ui](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                                    if (FAILED(result) || !controller) {
                                        return result;
                                    }
                                    g_controller = controller;
                                    g_controller->get_CoreWebView2(&g_webview);
                                    if (!g_webview) {
                                        return E_FAIL;
                                    }

                                    ComPtr<ICoreWebView2Settings> settings;
                                    if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings) {
                                        settings->put_AreDefaultContextMenusEnabled(TRUE);
                                        settings->put_AreDevToolsEnabled(TRUE);
                                        settings->put_IsStatusBarEnabled(FALSE);
                                        settings->put_IsZoomControlEnabled(FALSE);
                                    }

                                    ComPtr<ICoreWebView2_3> webview3;
                                    if (SUCCEEDED(g_webview.As(&webview3)) && webview3) {
                                        webview3->SetVirtualHostNameToFolderMapping(
                                            L"edity.app", ui.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
                                    }

                                    COREWEBVIEW2_COLOR bg{255, 11, 13, 16};
                                    ComPtr<ICoreWebView2Controller2> controller2;
                                    if (SUCCEEDED(g_controller.As(&controller2)) && controller2) {
                                        controller2->put_DefaultBackgroundColor(bg);
                                    }

                                    g_bridge = std::make_unique<edity::AppBridge>(g_hwnd, g_webview);
                                    BindMessages();
                                    ResizeWebView();
                                    g_webview->Navigate(UiUrl().c_str());
                                    return S_OK;
                                })
                                .Get());
            })
            .Get());
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(11, 13, 16));
    wc.lpszClassName = kClassName;
    wc.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                             LR_DEFAULTCOLOR | LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_DEFAULTCOLOR | LR_SHARED));
    if (!wc.hIcon) {
        wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    }
    if (!wc.hIconSm) {
        wc.hIconSm = wc.hIcon;
    }
    RegisterClassExW(&wc);

    const int width = 1440;
    const int height = 900;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"EDITY — Expansion Trader Desk",
                             WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) {
        return 1;
    }

    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(wc.hIcon));
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(wc.hIconSm));

    ApplyDarkFrame(g_hwnd);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    if (FAILED(CreateWebView())) {
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
