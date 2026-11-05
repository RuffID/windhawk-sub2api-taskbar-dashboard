#pragma once

#include "weekly.h"
#include "../core/state.h"
#include "../core/text_json.h"
#include "../core/time.h"

namespace sub2api_dashboard {

std::wstring UnixToIsoUtc(double seconds) {
    if (seconds <= 0 || seconds > 253402300799.0) {
        return {};
    }
    ULARGE_INTEGER ticks{};
    ticks.QuadPart = 116444736000000000ULL + static_cast<ULONGLONG>(seconds * 10000000.0);
    FILETIME time{ticks.LowPart, ticks.HighPart};
    SYSTEMTIME utc{};
    if (!FileTimeToSystemTime(&time, &utc)) {
        return {};
    }
    wchar_t text[32];
    swprintf_s(text, L"%04u-%02u-%02uT%02u:%02u:%02uZ", utc.wYear, utc.wMonth,
               utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    return text;
}

int WindowDisplayPercent(const QuotaWindow& window) {
    if (window.state != FieldState::Value || !window.remainingPercent.Known()) {
        return -1;
    }
    return static_cast<int>(std::floor(window.remainingPercent.value + 0.5));
}

std::wstring AccountQualityText(const AccountSnapshot& account, double now) {
    std::wstring text;
    if (IsAccountExcluded(account)) {
        if (account.status.Known() && account.status.value == L"error") {
            text = L"Ошибка аккаунта";
        } else {
            text = L"Отключён / недоступен";
        }
    } else if (!account.status.Known() || !account.schedulable.Known()) {
        text = L"Статус н/д";
    } else if (!account.discovered) {
        text = L"Ошибка списка";
    } else if (account.quotaRead == QuotaReadState::Failed) {
        text = L"Ошибка обновления";
    } else if (account.quotaRead == QuotaReadState::Pending) {
        text = L"Запрос не завершён";
    } else if (account.quotaRead == QuotaReadState::NotSupported) {
        return L"Лимит подписки н/д";
    } else if (!account.quota.fetchedAt.Known()) {
        return L"Время квоты н/д";
    } else if (!IsQuotaFresh(account.quota, now)) {
        text = L"Устарело";
    } else if (!HasSubscriptionIdentity(account.quota)) {
        text = L"Идентичность н/д";
    } else if (!IsWindowUsable(account.quota.weekly, account.quota, now)) {
        text = L"Weekly н/д / истёк";
    }
    if (account.quota.fetchedAt.Known() && now < account.quota.fetchedAt.value) {
        if (!text.empty()) { text += L" · "; }
        text += L"Неверное время";
    }
    return text;
}

std::vector<DashboardAccount> MakeDashboardAccounts(const AccountPoolSnapshot& pool, double now) {
    std::vector<DashboardAccount> result;
    for (const auto& account : pool.accounts) {
        DashboardAccount card;
        card.alias = L"Account " + std::to_wstring(account.localId);
        if (account.name.Known() && !TrimString(account.name.value).empty()) {
            card.alias = account.name.value;
        }
        if (account.quota.email.Known()) {
            card.email = account.quota.email.value;
        } else if (account.email.Known()) {
            card.email = account.email.value;
        }
        card.hasPrimary = account.quota.fiveHour.state == FieldState::Value;
        const QuotaWindow* primaryWindow = &account.quota.fiveHour;
        // The long card slot prefers weekly; monthly never enters the mean.
        const QuotaWindow* longWindow = &account.quota.weekly;
        if (account.quota.weekly.state != FieldState::Value &&
            account.quota.monthly.state == FieldState::Value) {
            longWindow = &account.quota.monthly;
            card.secondaryIsMonthly = true;
        }
        if (!card.hasPrimary && account.quota.weekly.state == FieldState::Value &&
            account.quota.monthly.state == FieldState::Value) {
            // Both long windows are available, so retain both in the two slots.
            primaryWindow = &account.quota.monthly;
            card.hasPrimary = true;
            card.primaryLabel = L"Monthly";
        }
        card.primaryPercent = WindowDisplayPercent(*primaryWindow);
        card.secondaryPercent = WindowDisplayPercent(*longWindow);
        if (longWindow->state == FieldState::Value && longWindow->remainingPercent.Known()) {
            card.secondarySortPercent = longWindow->remainingPercent.value;
        }
        if (primaryWindow->resetAt.Known()) {
            card.primaryResetText = FormatRemainingCompact(UnixToIsoUtc(primaryWindow->resetAt.value));
        }
        if (longWindow->resetAt.Known()) {
            auto reset = UnixToIsoUtc(longWindow->resetAt.value);
            card.secondaryResetText = FormatRemainingCompact(reset);
            card.secondaryDaysText = FormatRemainingDays(reset);
        }
        if (account.quota.availableResetCredits.Known()) {
            card.availableResetCredits = account.quota.availableResetCredits.value;
            card.resetCreditNearestExpiresAt = account.quota.nearestResetExpiry;
        }
        card.dataQualityText = AccountQualityText(account, now);
        result.push_back(std::move(card));
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        double leftPercent = left.secondarySortPercent;
        double rightPercent = right.secondarySortPercent;
        if (leftPercent < 0) { leftPercent = 101; }
        if (rightPercent < 0) { rightPercent = 101; }
        return leftPercent < rightPercent;
    });
    return result;
}

std::wstring WeeklyDashboardText(const WeeklySummary& summary) {
    std::wstring text = FormatRemainingText(summary.percent);
    if (summary.incomplete) {
        text += L"*";
    }
    return text;
}

std::wstring PoolCountText(const WeeklySummary& summary) {
    std::wstring text = std::to_wstring(summary.usable) + L"/";
    text += std::to_wstring(summary.expected);
    if (!summary.expectedIsExact) { text += L"?"; }
    return text + L" подп.";
}

// Caller holds g_dashboardMutex. Both UI surfaces consume these projections
// from the same published pool; no independent averaging happens in renderers.
void PublishPoolProjection(AccountPoolSnapshot pool, double now, bool allowSuccessTime) {
    pool.summary = CalculateWeeklySummary(pool, now);
    pool.fullySuccessful = pool.fullySuccessful && !pool.summary.incomplete;
    auto cards = MakeDashboardAccounts(pool, now);
    auto text = WeeklyDashboardText(pool.summary);
    auto count = PoolCountText(pool.summary);
    std::wstring updated = g_lastUpdatedText;
    if (allowSuccessTime && pool.fullySuccessful && !pool.summary.incomplete) {
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t buffer[16];
        swprintf_s(buffer, L"%02u:%02u:%02u", time.wHour, time.wMinute, time.wSecond);
        updated = buffer;
    }
    g_accountPoolSnapshot = std::move(pool);
    g_dashboardAccounts = std::move(cards);
    g_dashboardText = std::move(text);
    g_poolCountText = std::move(count);
    g_lastUpdatedText = std::move(updated);
    g_apiKeyRemainingPercent = g_accountPoolSnapshot.summary.percent;
    g_accountsRequestCompleted = true;
    g_accountsAvailable = g_accountPoolSnapshot.discoveryComplete;
}

}  // namespace sub2api_dashboard
