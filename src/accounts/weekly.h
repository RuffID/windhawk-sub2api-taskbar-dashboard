#pragma once

#include "../core/account_data.h"
#include <map>

namespace sub2api_dashboard {

constexpr double QUOTA_CLOCK_SKEW_TOLERANCE_SECONDS = 5.0;

bool IsQuotaTimeTooFarAhead(const AccountQuota& quota, double now) {
    return quota.fetchedAt.Known() &&
           quota.fetchedAt.value - now > QUOTA_CLOCK_SKEW_TOLERANCE_SECONDS;
}

bool IsQuotaFresh(const AccountQuota& quota, double now) {
    // Separate server/Windows clocks can put a newly fetched quota slightly ahead.
    return quota.fetchedAt.Known() && !IsQuotaTimeTooFarAhead(quota, now) &&
           now - quota.fetchedAt.value <= 900.0;
}

bool IsAccountExcluded(const AccountSnapshot& account) {
    return (account.status.Known() && account.status.value != L"active") ||
           (account.schedulable.Known() && !account.schedulable.value);
}

bool HasSubscriptionIdentity(const AccountQuota& quota) {
    return quota.upstreamAccountId.Known() && !quota.upstreamAccountId.value.empty() &&
           quota.upstreamUserId.Known() && !quota.upstreamUserId.value.empty();
}

bool IsWindowUsable(const QuotaWindow& window, const AccountQuota& quota, double now) {
    return window.state == FieldState::Value && window.remainingPercent.Known() &&
           IsQuotaFresh(quota, now) && window.resetAt.state != FieldState::Invalid &&
           (!window.resetAt.Known() || window.resetAt.value > now);
}

bool IsWeeklyUsable(const AccountSnapshot& account, double now) {
    return !IsAccountExcluded(account) && account.status.Known() &&
           account.schedulable.Known() && account.type.Known() &&
           account.type.value == L"oauth" && HasSubscriptionIdentity(account.quota) &&
           account.quota.weekly.durationSeconds.Known() &&
           account.quota.weekly.durationSeconds.value == 604800 &&
           IsWindowUsable(account.quota.weekly, account.quota, now);
}

WeeklySummary CalculateWeeklySummary(const AccountPoolSnapshot& pool, double now) {
    WeeklySummary result;
    result.expectedIsExact = pool.discoveryComplete;
    result.incomplete = !pool.discoveryComplete || !pool.quotaReadsSucceeded;
    using Identity = std::pair<std::wstring, std::wstring>;
    std::map<Identity, const AccountSnapshot*> subscriptions;
    size_t unknownIdentities = 0;
    for (const auto& account : pool.accounts) {
        if (IsAccountExcluded(account)) {
            continue;
        }
        if (!HasSubscriptionIdentity(account.quota)) {
            unknownIdentities++;
            continue;
        }
        Identity identity{account.quota.upstreamAccountId.value, account.quota.upstreamUserId.value};
        auto& best = subscriptions[identity];
        if (!IsWeeklyUsable(account, now)) {
            continue;
        }
        if (!best || account.quota.fetchedAt.value > best->quota.fetchedAt.value ||
            (account.quota.fetchedAt.value == best->quota.fetchedAt.value &&
             account.localId < best->localId)) {
            best = &account;
        }
    }
    size_t unseen = 0;
    if (!pool.discoveryComplete && pool.expectedRecords > pool.accounts.size()) {
        unseen = pool.expectedRecords - pool.accounts.size();
    }
    result.expected = subscriptions.size() + unknownIdentities + unseen;
    result.expectedIsExact = result.expectedIsExact && unknownIdentities == 0;
    // An unknown remainder cannot have an exact denominator, even when no total arrived.
    long double sum = 0;
    for (const auto& entry : subscriptions) {
        const auto* account = entry.second;
        if (!account) {
            continue;
        }
        result.usable++;
        sum += static_cast<long double>(account->quota.weekly.remainingPercent.value);
        if (result.oldestFetchedAt == 0 || account->quota.fetchedAt.value < result.oldestFetchedAt) {
            result.oldestFetchedAt = account->quota.fetchedAt.value;
        }
    }
    if (result.usable > 0) {
        result.percent = static_cast<int>(std::floor(sum / result.usable + 0.5L));
    }
    result.incomplete = result.incomplete || result.usable != result.expected;
    return result;
}

}  // namespace sub2api_dashboard
