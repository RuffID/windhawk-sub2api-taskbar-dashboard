#pragma once

#include "parser.h"
#include "weekly.h"
#include <functional>
#include <map>
#include <thread>

namespace sub2api_dashboard {

// Request returns only the envelope's data, never logs bodies. The production
// adapter supplies authentication/deadline; fixtures supply in-memory replies.
using AccountRequest = std::function<bool(const std::wstring&, wdj::JsonObject&, DWORD&)>;
using AccountStop = std::function<bool()>;
using AccountClock = std::function<double()>;
constexpr size_t QUOTA_CONCURRENCY = 4;

AccountPoolSnapshot DiscoverAccountPool(const AccountRequest& request, const AccountStop& stop) {
    AccountPoolSnapshot result;
    std::map<int64_t, size_t> seen;
    size_t lastPage = 1;
    size_t initialTotal = 0;
    size_t initialPageSize = 0;
    bool complete = true;
    for (size_t pageNumber = 1; pageNumber <= lastPage; pageNumber++) {
        if (stop()) {
            complete = false;
            break;
        }
        wdj::JsonObject data = nullptr;
        DWORD status = 0;
        AccountPage page;
        bool received = false;
        try {
            received = request(L"/accounts?platform=openai&page_size=100&page=" +
                               std::to_wstring(pageNumber), data, status) &&
                       ParseAccountPage(data, pageNumber, page);
        } catch (...) {
            received = false;
        }
        if (!received) {
            complete = false;
            if (!stop()) {
                Wh_Log(L"Account discovery failed: page=%llu status=%u",
                       static_cast<unsigned long long>(pageNumber), status);
            }
            continue;
        }
        if (pageNumber == 1) {
            initialTotal = page.total;
            initialPageSize = page.pageSize;
        } else if (page.total != initialTotal || page.pageSize != initialPageSize) {
            complete = false;
        }
        lastPage = std::max(lastPage, page.pages);
        result.expectedRecords = std::max(result.expectedRecords, page.total);
        complete = complete && page.itemsValid;
        for (auto& account : page.accounts) {
            auto existing = seen.find(account.localId);
            if (existing != seen.end()) {
                complete = false;
                // A moving page boundary can return conflicting metadata. Keep
                // the card, but do not trust either status for the average.
                auto& first = result.accounts[existing->second];
                if (first.status.state != account.status.state || first.status.value != account.status.value ||
                    first.schedulable.state != account.schedulable.state ||
                    first.schedulable.value != account.schedulable.value) {
                    first.status.state = FieldState::Invalid;
                    first.schedulable.state = FieldState::Invalid;
                }
                continue;
            }
            seen.emplace(account.localId, result.accounts.size());
            result.accounts.push_back(std::move(account));
        }
    }
    result.discoveryComplete = complete && result.accounts.size() == result.expectedRecords;
    return result;
}

// All workers belong to this stack frame. Join also runs during unwinding or
// partial thread creation; request contexts stay alive until their HTTP closes.
struct QuotaWorkers {
    std::atomic<bool> stopped{false};
    std::array<std::thread, QUOTA_CONCURRENCY> threads;
    void Join() noexcept {
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    ~QuotaWorkers() {
        stopped = true;
        Join();
    }
};

AccountPoolSnapshot LoadAccountPool(const AccountPoolSnapshot& previous,
                                    const AccountRequest& request,
                                    const AccountStop& stop,
                                    const AccountClock& clock) {
    auto result = DiscoverAccountPool(request, stop);
    std::map<int64_t, const AccountSnapshot*> old;
    for (const auto& account : previous.accounts) {
        old.emplace(account.localId, &account);
    }
    std::atomic<size_t> next{0};
    std::atomic<bool> readsSucceeded{true};
    QuotaWorkers workers;
    try {
        const size_t workerCount = std::min(QUOTA_CONCURRENCY, result.accounts.size());
        for (size_t worker = 0; worker < workerCount; worker++) {
            workers.threads[worker] = std::thread([&] {
                bool apartment = false;
                try {
                    winrt::init_apartment(winrt::apartment_type::multi_threaded);
                    apartment = true;
                    while (!workers.stopped && !stop()) {
                        size_t index = next.fetch_add(1);
                        if (index >= result.accounts.size()) {
                            break;
                        }
                        auto& account = result.accounts[index];
                        if (account.quotaRead != QuotaReadState::Pending) {
                            continue;
                        }
                        wdj::JsonObject data = nullptr;
                        DWORD status = 0;
                        bool received = false;
                        try {
                            received = request(L"/openai/accounts/" + std::to_wstring(account.localId) +
                                               L"/quota", data, status);
                            if (received) {
                                // Replace the whole projection: revoked identity is never
                                // silently restored from an earlier successful response.
                                account.quota = ParseAccountQuota(data, clock());
                                account.quotaRead = QuotaReadState::Succeeded;
                            }
                        } catch (...) {
                            received = false;
                        }
                        if (!received) {
                            account.quotaRead = QuotaReadState::Failed;
                            readsSucceeded = false;
                            auto cached = old.find(account.localId);
                            if (cached != old.end() && cached->second->type.Known() &&
                                cached->second->type.value == L"oauth" &&
                                IsQuotaFresh(cached->second->quota, clock())) {
                                account.quota = cached->second->quota;
                            }
                            if (!stop()) {
                                Wh_Log(L"Quota query failed: id=%lld status=%u",
                                       static_cast<long long>(account.localId), status);
                            }
                        } else if (!IsQuotaFresh(account.quota, clock())) {
                            readsSucceeded = false;
                        }
                    }
                } catch (...) {
                    readsSucceeded = false;
                    workers.stopped = true;
                    Wh_Log(L"Quota worker failed");
                }
                if (apartment) {
                    winrt::uninit_apartment();
                }
            });
        }
    } catch (...) {
        readsSucceeded = false;
        workers.stopped = true;
        Wh_Log(L"Quota workers unavailable");
    }
    workers.Join();
    result.quotaReadsSucceeded = readsSucceeded;
    for (auto& account : result.accounts) {
        if (account.quotaRead == QuotaReadState::Pending) {
            result.quotaReadsSucceeded = false;
            account.quotaRead = QuotaReadState::Failed;
            auto cached = old.find(account.localId);
            if (cached != old.end() && cached->second->type.Known() &&
                cached->second->type.value == L"oauth" && IsQuotaFresh(cached->second->quota, clock())) {
                account.quota = cached->second->quota;
            }
        }
    }
    if (!result.discoveryComplete) {
        std::map<int64_t, bool> present;
        for (const auto& account : result.accounts) {
            present.emplace(account.localId, true);
        }
        // Only a complete enumeration can prove removal. Retain missing cards
        // with explicit discovery error; their original timestamp still expires.
        for (const auto& account : previous.accounts) {
            if (present.find(account.localId) == present.end()) {
                auto retained = account;
                retained.discovered = false;
                retained.quotaRead = QuotaReadState::Failed;
                if (!IsQuotaFresh(retained.quota, clock())) {
                    retained.quota = {};
                }
                result.accounts.push_back(std::move(retained));
            }
        }
    }
    result.interrupted = stop();
    result.summary = CalculateWeeklySummary(result, clock());
    result.fullySuccessful = !result.interrupted && result.discoveryComplete &&
                             result.quotaReadsSucceeded && !result.summary.incomplete;
    return result;
}

}  // namespace sub2api_dashboard
