#pragma once

#include "windows.h"
#include "../core/refresh_api.h"
#include "../config/settings.h"
#include "../network/http.h"
#include "../versions/release_cache.h"

namespace sub2api_dashboard {

struct DashboardRefreshFlagGuard {
    ~DashboardRefreshFlagGuard() {
        g_dashboardRefreshInProgress = false;
    }
};

void QueueDashboardRefresh() {
    if (g_unloading || g_dashboardUpdatesPaused) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_dashboardRefreshWorkMutex);
    if (g_unloading || g_dashboardUpdatesPaused) {
        return;
    }

    if (!g_dashboardRefreshWork) {
        g_dashboardRefreshWork = CreateThreadpoolWork(
            [](PTP_CALLBACK_INSTANCE, PVOID, PTP_WORK) {
                RefreshDashboardText();
            },
            nullptr, nullptr);
        if (!g_dashboardRefreshWork) {
            Wh_Log(L"CreateThreadpoolWork failed: %u", GetLastError());
            return;
        }
    }

    SubmitThreadpoolWork(g_dashboardRefreshWork);
}

void StopDashboardRefreshWork() {
    std::lock_guard<std::mutex> lock(g_dashboardRefreshWorkMutex);
    if (!g_dashboardRefreshWork) {
        return;
    }

    WaitForThreadpoolWorkCallbacks(g_dashboardRefreshWork, TRUE);
    CloseThreadpoolWork(g_dashboardRefreshWork);
    g_dashboardRefreshWork = nullptr;
}

void RefreshDashboardText() {
    if (IsDashboardRefreshCancelled() || g_dashboardRefreshInProgress.exchange(true)) {
        return;
    }
    DashboardRefreshFlagGuard refreshGuard;
    try {
        Settings settings = GetSettingsSnapshot();
        std::wstring text = GetFoundationText(settings);
        // Stage 02 only publishes the connection state. Account/average/version
        // requests will be connected in stages 03-04, using this one snapshot.
        {
            std::lock_guard<std::mutex> lock(g_dashboardMutex);
            if (IsDashboardRefreshCancelled()) {
                return;
            }
            g_dashboardText = text;
            g_apiKeyRemainingPercent = -1;
            g_lastUpdatedText = L"—";
            g_accountsRequestCompleted = false;
            g_accountsAvailable = false;
            g_dashboardAccounts.clear();
        }
        if (IsDashboardRefreshCancelled()) {
            return;
        }
        PushDashboardTextToTaskbar(text);
        HWND popupWindow = g_popupWindow.load();
        if (popupWindow && IsWindow(popupWindow)) {
            PostMessage(popupWindow, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
        }
    } catch (...) {
        LogException(L"RefreshDashboardText", CurrentExceptionHResult());
    }
}

void StopDashboardTimer() {
    if (!g_dashboardTimer) {
        return;
    }

    SetThreadpoolTimer(g_dashboardTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(g_dashboardTimer, TRUE);
    CloseThreadpoolTimer(g_dashboardTimer);
    g_dashboardTimer = nullptr;
}

void StartDashboardTimer() {
    StopDashboardTimer();

    Settings settings = GetSettingsSnapshot();

    g_dashboardTimer = CreateThreadpoolTimer(
        [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) { RefreshDashboardText(); },
        nullptr, nullptr);

    if (!g_dashboardTimer) {
        winrt::throw_last_error();
    }

    FILETIME dueTime;
    ULARGE_INTEGER dueTimeValue;
    dueTimeValue.QuadPart = static_cast<ULONGLONG>(-10000000LL);
    dueTime.dwLowDateTime = dueTimeValue.LowPart;
    dueTime.dwHighDateTime = dueTimeValue.HighPart;

    DWORD updateIntervalMs = settings.updateIntervalSeconds * 1000UL;
    SetThreadpoolTimer(g_dashboardTimer, &dueTime,
                       updateIntervalMs, 0);
}

void PauseDashboardUpdates() {
    g_dashboardUpdatesPaused = true;
    if (g_dashboardCancelEvent && !SetEvent(g_dashboardCancelEvent)) {
        winrt::throw_last_error();
    }
    StopDashboardTimer();
    StopDashboardRefreshWork();
}

void StopInitializeTimer() {
    if (!g_initializeTimer) {
        return;
    }

    SetThreadpoolTimer(g_initializeTimer, nullptr, 0, 0);
    WaitForThreadpoolTimerCallbacks(g_initializeTimer, TRUE);
    CloseThreadpoolTimer(g_initializeTimer);
    g_initializeTimer = nullptr;
}

void StartInitializeTimer() {
    StopInitializeTimer();

    g_initializeAttempts = 0;
    g_taskbarRecoveryRequested = true;
    g_initializeTimer = CreateThreadpoolTimer(
        [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) {
            try {
                if (g_unloading || g_taskbarRecoveryInProgress.exchange(true)) {
                    return;
                }
                struct RecoveryGuard {
                    ~RecoveryGuard() {
                        g_taskbarRecoveryInProgress = false;
                    }
                } recoveryGuard;
                int attempt = ++g_initializeAttempts;
                bool recoveryRequested = g_taskbarRecoveryRequested.exchange(false);
                if (!recoveryRequested && attempt % 5 != 0) {
                    return;
                }
                if (g_initialized && g_widgetCount == 0 && attempt % 5 == 0) {
                    Wh_Log(
                        L"Widget wasn't added yet, reconnecting XAML watcher");
                    UninitializeSettingsAndTap();
                }

                TryInitializeForTaskbar();
            } catch (...) {
                LogException(L"InitializeTimer", CurrentExceptionHResult());
            }
        },
        nullptr, nullptr);

    if (!g_initializeTimer) {
        winrt::throw_last_error();
    }

    FILETIME dueTime;
    ULARGE_INTEGER dueTimeValue;
    dueTimeValue.QuadPart = static_cast<ULONGLONG>(-10000000LL);
    dueTime.dwLowDateTime = dueTimeValue.LowPart;
    dueTime.dwHighDateTime = dueTimeValue.HighPart;

    SetThreadpoolTimer(g_initializeTimer, &dueTime, 1000, 0);
}

}  // namespace sub2api_dashboard

