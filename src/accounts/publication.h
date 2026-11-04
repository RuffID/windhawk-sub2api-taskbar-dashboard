#pragma once

#include "presentation.h"
#include "../network/http.h"

namespace sub2api_dashboard {

// Caller holds the publication mutex. Emit only safe counts on state changes.
void LogPoolCompleteness(const AccountPoolSnapshot& pool) {
    if (pool.summary.incomplete &&
        (!g_accountsRequestCompleted || !g_accountPoolSnapshot.summary.incomplete ||
         pool.summary.usable != g_accountPoolSnapshot.summary.usable ||
         pool.summary.expected != g_accountPoolSnapshot.summary.expected)) {
        Wh_Log(L"Snapshot incomplete: usable=%llu expected=%llu discovery=%u",
               static_cast<unsigned long long>(pool.summary.usable),
               static_cast<unsigned long long>(pool.summary.expected),
               static_cast<unsigned>(pool.discoveryComplete));
    }
}

bool TryPublishAccountPool(AccountPoolSnapshot pool, uint64_t generation,
                           double now, bool allowSuccessTime) {
    std::lock_guard<std::mutex> lock(g_dashboardMutex);
    if (IsDashboardRefreshCancelled() || generation != g_dashboardGeneration) { return false; }
    pool.summary = CalculateWeeklySummary(pool, now);
    LogPoolCompleteness(pool);
    PublishPoolProjection(std::move(pool), now, allowSuccessTime);
    return true;
}

}  // namespace sub2api_dashboard
