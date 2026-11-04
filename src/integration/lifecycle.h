#pragma once

#include "runtime.h"
#include "../config/settings.h"

namespace sub2api_dashboard {

// Continue independent cleanup steps even if one UI/COM operation fails.
template <typename Action>
void CleanupStep(PCWSTR area, Action action) noexcept {
    try {
        action();
    } catch (...) {
        LogException(area, CurrentExceptionHResult());
    }
}

void StopModResources() noexcept {
    g_unloading = true;
    g_dashboardUpdatesPaused = true;
    CleanupStep(L"CancelRefresh", [] {
        if (g_dashboardCancelEvent && !SetEvent(g_dashboardCancelEvent)) {
            winrt::throw_last_error();
        }
    });
    CleanupStep(L"StopDashboardTimer", [] { StopDashboardTimer(); });
    CleanupStep(L"StopDashboardWork", [] { StopDashboardRefreshWork(); });
    CleanupStep(L"StopInitializeTimer", [] { StopInitializeTimer(); });
    CleanupStep(L"UninitializeTap", [] { UninitializeSettingsAndTap(); });
    CleanupStep(L"RemoveTaskbarWidgets", [] {
        // Include tracked owner threads even if Explorer replaced the host HWND.
        auto windows = GetXamlHostWindows();
        std::vector<DWORD> owners;
        {
            std::lock_guard<std::mutex> lock(g_widgetsMutex);
            for (const auto& widget : g_widgets) {
                owners.push_back(widget.threadId);
            }
        }
        for (DWORD owner : owners) {
            EnumThreadWindows(owner, [](HWND window, LPARAM parameter) -> BOOL {
                auto* result = reinterpret_cast<std::vector<HWND>*>(parameter);
                result->push_back(window);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&windows));
        }
        std::vector<DWORD> cleaned;
        for (HWND window : windows) {
            DWORD owner = GetWindowThreadProcessId(window, nullptr);
            if (!owner || std::find(cleaned.begin(), cleaned.end(), owner) != cleaned.end()) {
                continue;
            }
            if (RunFromWindowThread(window, [](PVOID) {
                    UninitializeForCurrentThread();
                }, nullptr)) {
                cleaned.push_back(owner);
            } else {
                Wh_Log(L"Taskbar cleanup dispatch failed: tid=%u", owner);
            }
        }
    });
    CleanupStep(L"DestroyPopup", [] {
        HWND window = g_popupWindow.load();
        if (window && !RunFromWindowThread(window, [](PVOID) {
                std::lock_guard<std::mutex> lock(g_popupCreationMutex);
                HidePopupWindow();
                HWND popup = g_popupWindow.load();
                if (popup && !DestroyWindow(popup)) {
                    winrt::throw_last_error();
                }
            }, nullptr)) {
            Wh_Log(L"Popup cleanup dispatch failed");
        }
        // Also remove the process-wide hook when its owner window disappeared.
        if (g_popupMouseHook) {
            UnhookWindowsHookEx(g_popupMouseHook);
            g_popupMouseHook = nullptr;
        }
        if (g_popupClassRegistered) {
            if (!UnregisterClass(L"Sub2ApiDashboardPopup", GetCurrentModuleHandle())) {
                winrt::throw_last_error();
            }
            g_popupClassRegistered = false;
        }
    });
    if (g_dashboardCancelEvent) {
        CloseHandle(g_dashboardCancelEvent);
        g_dashboardCancelEvent = nullptr;
    }
    CleanupStep(L"ClearSettings", [] {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        g_settings = {};
    });
}

BOOL InitializeMod() try {
    g_unloading = false;
    g_dashboardUpdatesPaused = true;
    g_dashboardCancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!g_dashboardCancelEvent) {
        winrt::throw_last_error();
    }
    LoadSettings();
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardText = GetFoundationText(GetSettingsSnapshot());
    }

    if (!WindhawkUtils::SetFunctionHook(CreateWindowExW, CreateWindowExW_Hook,
                                       &CreateWindowExW_Original)) {
        winrt::throw_hresult(E_FAIL);
    }

    // user32 is already loaded by Explorer; do not add a module reference.
    HMODULE user32 = GetModuleHandle(L"user32.dll");
    if (!user32) {
        winrt::throw_last_error();
    }
    auto createWindowInBand = reinterpret_cast<CreateWindowInBand_t>(
        GetProcAddress(user32, "CreateWindowInBand"));
    if (createWindowInBand &&
        !WindhawkUtils::SetFunctionHook(createWindowInBand, CreateWindowInBand_Hook,
                                       &CreateWindowInBand_Original)) {
        winrt::throw_hresult(E_FAIL);
    }
    auto createWindowInBandEx = reinterpret_cast<CreateWindowInBandEx_t>(
        GetProcAddress(user32, "CreateWindowInBandEx"));
    if (createWindowInBandEx &&
        !WindhawkUtils::SetFunctionHook(createWindowInBandEx, CreateWindowInBandEx_Hook,
                                       &CreateWindowInBandEx_Original)) {
        winrt::throw_hresult(E_FAIL);
    }
    return TRUE;
} catch (...) {
    LogException(L"Wh_ModInit", CurrentExceptionHResult());
    StopModResources();
    return FALSE;
}

void ApplySettingsChanged() try {
    // Signal, drain scheduled AND manual work, then replace settings. The event
    // stays signalled until every reader of the old snapshot has finished.
    PauseDashboardUpdates();
    LoadSettings();
    std::wstring text = GetFoundationText(GetSettingsSnapshot());
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardText = text;
        g_lastUpdatedText = L"—";
        g_apiKeyRemainingPercent = -1;
        g_accountsRequestCompleted = false;
        g_accountsAvailable = false;
        g_dashboardAccounts.clear();
        g_dashboardVersion = {};
    }
    if (!ResetEvent(g_dashboardCancelEvent)) {
        winrt::throw_last_error();
    }
    g_dashboardUpdatesPaused = false;
    StartDashboardTimer();
    PushDashboardTextToTaskbar(text);
    HWND popup = g_popupWindow.load();
    if (popup) {
        PostMessage(popup, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
    }
} catch (...) {
    LogException(L"Wh_ModSettingsChanged", CurrentExceptionHResult());
    CleanupStep(L"PauseAfterSettingsFailure", [] { PauseDashboardUpdates(); });
}

void AfterInitializeMod() try {
    g_dashboardUpdatesPaused = false;
    StartDashboardTimer();
    TryInitializeForTaskbar();
    StartInitializeTimer();
} catch (...) {
    LogException(L"Wh_ModAfterInit", CurrentExceptionHResult());
    StopModResources();
}

void UninitializeMod() noexcept {
    StopModResources();
}

}  // namespace sub2api_dashboard

