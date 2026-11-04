#pragma once

#include "windows.h"
#include "../core/refresh_api.h"
#include "../config/settings.h"
#include "../network/http.h"
#include "../versions/release_cache.h"
#include "../accounts/pool.h"
#include "../accounts/publication.h"

namespace sub2api_dashboard {

struct DashboardRefreshFlagGuard {
    ~DashboardRefreshFlagGuard() {
        g_dashboardRefreshInProgress = false;
    }
};

struct RefreshApartmentGuard {
    RefreshApartmentGuard() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~RefreshApartmentGuard() { winrt::uninit_apartment(); }
};

void NotifyDashboardProjection() {
    if (IsDashboardRefreshCancelled()) {
        return;
    }
    // The dispatch reads the currently published text on the owning UI thread,
    // so a delayed notification cannot repaint an older pool over a newer one.
    PushDashboardTextToTaskbar({});
    HWND popupWindow = g_popupWindow.load();
    if (popupWindow && IsWindow(popupWindow)) {
        PostMessage(popupWindow, WM_REFRESH_DASHBOARD_POPUP, 0, 0);
    }
}

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
        if (GetConnectionState(settings) != ConnectionState::Ready) {
            return;
        }
        RefreshApartmentGuard apartment;
        const ULONGLONG deadline = CreateRefreshDeadline(settings);
        AccountPoolSnapshot previous;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(g_dashboardMutex);
            previous = g_accountPoolSnapshot;
            generation = g_dashboardGeneration;
        }
        auto pool = LoadAccountPool(previous,
            [&](const std::wstring& resource, wdj::JsonObject& data, DWORD& status) {
                return Sub2ApiGet(settings, resource, deadline, data, &status);
            },
            [&] { return IsDashboardRefreshCancelled() || GetTickCount64() >= deadline; },
            [] { return UnixNowSeconds(); });
        if (!TryPublishAccountPool(std::move(pool), generation, UnixNowSeconds(), true)) { return; }
        NotifyDashboardProjection();
    } catch (...) {
        LogException(L"RefreshDashboardText", CurrentExceptionHResult());
        try {
            {
                std::lock_guard<std::mutex> lock(g_dashboardMutex);
                if (IsDashboardRefreshCancelled()) { return; }
                auto failed = g_accountPoolSnapshot;
                failed.discoveryComplete = false;
                failed.quotaReadsSucceeded = false;
                failed.fullySuccessful = false;
                for (auto& account : failed.accounts) {
                    account.discovered = false;
                    account.quotaRead = QuotaReadState::Failed;
                }
                PublishPoolProjection(std::move(failed), UnixNowSeconds(), false);
            }
            NotifyDashboardProjection();
        } catch (...) {
            LogException(L"PublishRefreshFailure", CurrentExceptionHResult());
        }
    }
}

void RefreshDashboardAge() try {
    if (IsDashboardRefreshCancelled() || g_dashboardAgeInProgress.exchange(true)) { return; }
    struct AgeGuard {
        ~AgeGuard() { g_dashboardAgeInProgress = false; }
    } ageGuard;
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        if (IsDashboardRefreshCancelled() || !g_accountsRequestCompleted) { return; }
        auto aged = g_accountPoolSnapshot;
        aged.summary = CalculateWeeklySummary(aged, UnixNowSeconds());
        LogPoolCompleteness(aged);
        PublishPoolProjection(std::move(aged), UnixNowSeconds(), false);
    }
    NotifyDashboardProjection();
} catch (...) {
    LogException(L"RefreshDashboardAge", CurrentExceptionHResult());
}

void StopDashboardTimer() {
    if (g_dashboardAgeTimer) {
        SetThreadpoolTimer(g_dashboardAgeTimer, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(g_dashboardAgeTimer, TRUE);
        CloseThreadpoolTimer(g_dashboardAgeTimer);
        g_dashboardAgeTimer = nullptr;
    }
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
    // Age is independent of the configured network interval. No HTTP here.
    g_dashboardAgeTimer = CreateThreadpoolTimer(
        [](PTP_CALLBACK_INSTANCE, PVOID, PTP_TIMER) { RefreshDashboardAge(); },
        nullptr, nullptr);
    if (!g_dashboardAgeTimer) { winrt::throw_last_error(); }
    SetThreadpoolTimer(g_dashboardAgeTimer, &dueTime, 1000, 0);
}

void PauseDashboardUpdates() {
    {
        std::lock_guard<std::mutex> lock(g_dashboardMutex);
        g_dashboardUpdatesPaused = true;
        g_dashboardGeneration++;
        if (g_dashboardCancelEvent && !SetEvent(g_dashboardCancelEvent)) {
            winrt::throw_last_error();
        }
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

