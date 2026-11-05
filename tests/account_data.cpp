// Use Windhawk's no-host API stubs; this executable never loads a mod or UI.
#ifndef WH_EDITING
#define WH_EDITING
#endif

#include "../src/accounts/pool.h"
#include "../src/accounts/publication.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <stdexcept>

using namespace sub2api_dashboard;
constexpr double NOW = 2000000000;

void Require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

wdj::JsonObject Json(const std::wstring& text) {
    wdj::JsonObject result = nullptr;
    Require(wdj::JsonObject::TryParse(text, result), "fixture JSON invalid");
    return result;
}

std::wstring Row(int id, PCWSTR status = L"active", bool schedulable = true,
                  PCWSTR type = L"oauth") {
    return L"{\"id\":" + std::to_wstring(id) +
        L",\"platform\":\"openai\",\"name\":\"Fixture\",\"type\":\"" + type +
        L"\",\"status\":\"" + status + L"\",\"schedulable\":" +
        (schedulable ? L"true" : L"false") +
        L",\"credentials\":{\"email\":\"fixture@example.invalid\"}}";
}

wdj::JsonObject Page(int page, int total, const std::wstring& items, int size = 100) {
    return Json(L"{\"page\":" + std::to_wstring(page) + L",\"page_size\":" +
        std::to_wstring(size) + L",\"total\":" + std::to_wstring(total) + L",\"pages\":" +
        std::to_wstring(std::max(1, (total + size - 1) / size)) + L",\"items\":[" + items + L"]}");
}

std::wstring Rows(int first, int last) {
    std::wstring rows;
    for (int id = first; id <= last; id++) {
        if (!rows.empty()) { rows += L","; }
        rows += Row(id);
    }
    return rows;
}

std::wstring Window(PCWSTR used, int seconds, double reset = NOW + 10000) {
    return L"{\"used_percent\":" + std::wstring(used) +
        L",\"limit_window_seconds\":" + std::to_wstring(seconds) +
        L",\"reset_at\":" + std::to_wstring(static_cast<long long>(reset)) + L"}";
}

wdj::JsonObject Quota(PCWSTR used = L"20", double fetched = NOW,
                       PCWSTR account = L"workspace-fixture", PCWSTR user = L"user-fixture") {
    return Json(L"{\"account_id\":\"" + std::wstring(account) + L"\",\"user_id\":\"" + user +
        L"\",\"fetched_at\":" + std::to_wstring(static_cast<long long>(fetched)) +
        L",\"rate_limit\":{\"primary_window\":" + Window(used, 604800) + L"}}");
}

AccountSnapshot Account(int id, PCWSTR used = L"20", double fetched = NOW,
                         PCWSTR account = L"workspace-fixture", PCWSTR user = L"user-fixture") {
    AccountPage page;
    Require(ParseAccountPage(Page(1, 1, Row(id)), 1, page), "account fixture parse failed");
    auto result = page.accounts.front();
    result.quota = ParseAccountQuota(Quota(used, fetched, account, user), NOW);
    result.quotaRead = QuotaReadState::Succeeded;
    return result;
}

AccountPoolSnapshot Pool(std::vector<AccountSnapshot> accounts) {
    AccountPoolSnapshot pool;
    pool.accounts = std::move(accounts);
    pool.discoveryComplete = true;
    pool.expectedRecords = pool.accounts.size();
    pool.summary = CalculateWeeklySummary(pool, NOW);
    pool.fullySuccessful = !pool.summary.incomplete;
    return pool;
}

const AccountStop NEVER_STOP = [] { return false; };
const AccountClock CLOCK = [] { return NOW; };

void TestPages() {
    // Arrange
    std::vector<std::wstring> paths;
    AccountRequest request = [&](const auto& path, auto& data, DWORD& status) {
        paths.push_back(path);
        status = 200;
        if (path.ends_with(L"page=1")) { data = Page(1, 101, Rows(1, 100)); }
        else { data = Page(2, 101, Row(101)); }
        return true;
    };
    // Act
    auto pool = DiscoverAccountPool(request, NEVER_STOP);
    // Assert
    Require(pool.discoveryComplete && pool.accounts.size() == 101 && paths.size() == 2,
            "all pages must be enumerated");
    Require(paths.front() == L"/accounts?platform=openai&page_size=100&page=1",
            "discovery must not filter group/status/type");

    // Arrange / Act
    auto empty = DiscoverAccountPool([](const auto&, auto& data, DWORD&) {
        data = Page(1, 0, L""); return true;
    }, NEVER_STOP);
    // Assert
    Require(empty.discoveryComplete && empty.accounts.empty(), "empty pool must succeed");

    // Arrange / Act
    auto repeated = DiscoverAccountPool([](const auto& path, auto& data, DWORD&) {
        if (path.ends_with(L"page=1")) { data = Page(1, 2, Row(1), 1); }
        else { data = Page(2, 2, Row(1), 1); }
        return true;
    }, NEVER_STOP);
    // Assert
    Require(!repeated.discoveryComplete && repeated.accounts.size() == 1,
            "duplicate local ID must not create a complete enumeration");

    // Arrange / Act
    auto changing = DiscoverAccountPool([](const auto& path, auto& data, DWORD&) {
        if (path.ends_with(L"page=1")) { data = Page(1, 2, Row(1), 1); }
        else if (path.ends_with(L"page=2")) { data = Page(2, 3, Row(2), 1); }
        else { data = Page(3, 3, Row(3), 1); }
        return true;
    }, NEVER_STOP);
    // Assert
    Require(!changing.discoveryComplete && changing.accounts.size() == 3,
            "growing enumeration must continue but remain incomplete");

    // Arrange / Act
    auto shrinking = DiscoverAccountPool([](const auto& path, auto& data, DWORD&) {
        if (path.ends_with(L"page=1")) { data = Page(1, 2, Row(1), 1); }
        else { data = Page(2, 0, L"", 1); }
        return true;
    }, NEVER_STOP);
    // Assert
    Require(!shrinking.discoveryComplete && shrinking.accounts.size() == 1,
            "shrinking enumeration must not claim complete success");

    // Arrange / Act
    auto failed = DiscoverAccountPool([](const auto& path, auto& data, DWORD& status) {
        if (path.ends_with(L"page=2")) { status = 503; return false; }
        if (path.ends_with(L"page=1")) { data = Page(1, 3, Row(1), 1); }
        else { data = Page(3, 3, Row(3), 1); }
        return true;
    }, NEVER_STOP);
    // Assert
    Require(!failed.discoveryComplete && failed.accounts.size() == 2,
            "failed middle page must not hide later known pages");
    AccountPage invalid;
    Require(!ParseAccountPage(Page(2, 1, Row(1)), 1, invalid), "wrong page must be rejected");
    Require(ParseAccountPage(Page(1, 1, L"{\"id\":0,\"platform\":\"openai\"}"), 1, invalid) &&
            !invalid.itemsValid, "invalid local ID must mark discovery incomplete");
}

void TestPagesWithoutCount() {
    // Arrange / Act / Assert: missing pages is derived at empty/exact/partial boundaries.
    for (int total : {0, 1, 100, 101, 200}) {
        auto data = Page(1, total, total == 0 ? L"" : Row(1));
        data.Remove(L"pages");
        AccountPage page;
        Require(ParseAccountPage(data, 1, page), "missing page count must parse");
        const size_t expected = total <= 100 ? 1 : 2;
        Require(page.pages == expected && page.total == static_cast<size_t>(total) &&
                page.page == 1 && page.pageSize == 100,
                "derived pagination must preserve metadata and round up");
    }

    // Absence must not mask malformed or contradictory metadata.
    for (PCWSTR value : {L"null", L"\"2\"", L"false", L"{}", L"[]",
                         L"0", L"-1", L"1.5", L"2147483648", L"1", L"3"}) {
        auto data = Json(L"{\"page\":1,\"page_size\":100,\"total\":101,\"items\":[],\"pages\":" +
                         std::wstring(value) + L"}");
        AccountPage page;
        Require(!ParseAccountPage(data, 1, page), "invalid supplied page count must be rejected");
    }
    for (PCWSTR field : {L"total", L"page", L"page_size", L"items"}) {
        auto data = Page(1, 1, Row(1));
        data.Remove(L"pages");
        data.Remove(field);
        AccountPage page;
        Require(!ParseAccountPage(data, 1, page), "required pagination fields must remain required");
    }
    for (PCWSTR value : {L"0", L"null", L"\"100\""}) {
        auto data = Json(L"{\"page\":1,\"total\":1,\"items\":[],\"page_size\":" +
                         std::wstring(value) + L"}");
        AccountPage page;
        Require(!ParseAccountPage(data, 1, page), "invalid page size must not be used for division");
    }

    // Arrange: the real loader must reach later pages and quota after discovery.
    std::vector<std::wstring> paths;
    std::atomic<int> quotaRequests{0};
    AccountRequest request = [&](const auto& path, auto& data, DWORD& status) {
        status = 200;
        if (path.starts_with(L"/accounts?")) {
            paths.push_back(path);
            if (path.ends_with(L"page=1")) { data = Page(1, 3, Rows(1, 2), 2); }
            else { data = Page(2, 3, Row(3), 2); }
            data.Remove(L"pages");
            return true;
        }
        for (int id = 1; id <= 3; id++) {
            if (path == L"/openai/accounts/" + std::to_wstring(id) + L"/quota") {
                auto identity = L"workspace-" + std::to_wstring(id);
                data = Quota(L"20", NOW, identity.c_str());
                quotaRequests++;
                return true;
            }
        }
        return false;
    };
    // Act
    auto pool = LoadAccountPool({}, request, NEVER_STOP, CLOCK);
    // Assert
    Require(paths.size() == 2 && paths[0] == L"/accounts?platform=openai&page_size=100&page=1" &&
            paths[1] == L"/accounts?platform=openai&page_size=100&page=2",
            "missing page count must enumerate every derived page");
    Require(pool.discoveryComplete && pool.expectedRecords == 3 && pool.accounts.size() == 3 &&
            quotaRequests == 3 && pool.fullySuccessful && pool.summary.usable == 3 &&
            pool.summary.expected == 3 && pool.summary.percent == 80 && !pool.summary.incomplete,
            "missing page count must allow quota loading and a complete summary");
}

void TestNormalization() {
    // Arrange
    auto data = Json(L"{\"fetched_at\":2000000000,\"account_id\":\"w\",\"user_id\":\"u\","
        L"\"rate_limit\":{\"primary_window\":" + Window(L"60", 604800) +
        L",\"secondary_window\":" + Window(L"0", 18000) + L"},"
        L"\"additional_rate_limits\":[{\"rate_limit\":{\"primary_window\":" + Window(L"99", 604800) + L"}}]}");
    // Act
    auto quota = ParseAccountQuota(data, NOW);
    // Assert
    Require(quota.weekly.remainingPercent.Known() && quota.weekly.remainingPercent.value == 40 &&
            quota.fiveHour.remainingPercent.value == 100, "windows must follow duration, not slot");
    auto monthly = ParseAccountQuota(Json(L"{\"rate_limit\":{\"primary_window\":" +
        Window(L"25", 2592000) + L"}}"), NOW);
    Require(monthly.monthly.remainingPercent.value == 75 &&
            !monthly.weekly.remainingPercent.Known(), "monthly must stay separate");

    // Arrange / Act
    auto missing = ParseAccountQuota(Json(L"{}"), NOW);
    auto nullQuota = ParseAccountQuota(Json(L"{\"rate_limit\":null,\"fetched_at\":null}"), NOW);
    auto invalid = ParseAccountQuota(Quota(L"\"0\""), NOW);
    auto zero = ParseAccountQuota(Quota(L"0"), NOW);
    auto over = ParseAccountQuota(Quota(L"125"), NOW);
    auto negative = ParseAccountQuota(Quota(L"-1"), NOW);
    // Assert
    Require(missing.fetchedAt.state == FieldState::Missing &&
            nullQuota.fetchedAt.state == FieldState::Null &&
            nullQuota.weekly.state == FieldState::Null, "missing and null must remain distinct");
    Require(invalid.weekly.remainingPercent.state == FieldState::Invalid &&
            zero.weekly.remainingPercent.Known() && zero.weekly.remainingPercent.value == 100 &&
            over.weekly.remainingPercent.value == 0 && !negative.weekly.remainingPercent.Known(),
            "used percent validation must not fabricate zero or full remainder");
    auto slots = ParseAccountQuota(Json(L"{\"rate_limit\":{\"primary_window\":null,\"secondary_window\":4}}"), NOW);
    Require(slots.primarySlot.state == FieldState::Null && slots.secondarySlot.state == FieldState::Invalid,
            "slot absence/null/type must remain inspectable");
    auto noDuration = ParseAccountQuota(Json(L"{\"rate_limit\":{\"primary_window\":{\"used_percent\":0}}}"), NOW);
    Require(!noDuration.weekly.remainingPercent.Known(), "missing duration must not imply weekly");
    auto duplicate = ParseAccountQuota(Json(L"{\"rate_limit\":{\"primary_window\":" + Window(L"20", 604800) +
        L",\"secondary_window\":" + Window(L"40", 604800) + L"}}"), NOW);
    Require(duplicate.weekly.state == FieldState::Invalid, "conflicting weekly slots are ambiguous");

    // Arrange / Act
    auto credits = ParseAccountQuota(Json(L"{\"rate_limit_reset_credits\":{\"available_count\":3,\"credits\":["
        L"{\"expires_at\":\"2034-01-01T00:00:00Z\"},{\"expires_at\":\"2033-06-01T00:00:00Z\"},"
        L"{\"expires_at\":\"2020-01-01T00:00:00Z\"}]}}"), NOW);
    // Assert
    Require(credits.availableResetCredits.Known() && credits.availableResetCredits.value == 3 &&
            credits.nearestResetExpiry == L"2033-06-01T00:00:00Z", "nearest future Reset expiry required");
    auto unknownExpiry = ParseAccountQuota(Json(L"{\"rate_limit_reset_credits\":{\"available_count\":1}}"), NOW);
    Require(unknownExpiry.availableResetCredits.value == 1 && unknownExpiry.nearestResetExpiry.empty(),
            "unknown Reset expiry must not erase known count");
    Require(ParseAccountQuota(Json(L"{\"rate_limit_reset_credits\":null}"), NOW).availableResetCredits.state ==
            FieldState::Null, "unknown Reset count must remain unknown");
    auto partialCredits = ParseAccountQuota(Json(L"{\"rate_limit_reset_credits\":{\"credits\":["
        L"{\"expires_at\":\"2033-06-01T00:00:00.9Z\"},{\"expires_at\":\"2033-06-01T00:00:00.1Z\"}]}}"), NOW);
    Require(!partialCredits.availableResetCredits.Known() &&
            partialCredits.nearestResetExpiry == L"2033-06-01T00:00:00.1Z",
            "Reset count and expiry are independent; fractional expiry must retain ordering");

    // Arrange / Act: full DTO may contain extra fields; scoped DTO may omit them.
    AccountPage full, scoped;
    Require(ParseAccountPage(Page(1, 1, L"{\"id\":1,\"platform\":\"openai\",\"type\":\"oauth\","
        L"\"status\":\"active\",\"schedulable\":true,\"parent_account_id\":7,"
        L"\"credentials\":{\"email\":\"fixture@example.invalid\",\"unused\":{\"nested\":true}},"
        L"\"extra\":{\"codex_7d_used_percent\":0,\"codex_usage_updated_at\":\"2033-05-18T03:33:20Z\"}}"), 1, full),
        "full projection must parse");
    Require(ParseAccountPage(Page(1, 1, Row(1)), 1, scoped), "scoped projection must parse");
    // Assert
    Require(full.accounts[0].email.value == scoped.accounts[0].email.value &&
            !full.accounts[0].quota.fetchedAt.Known() && !full.accounts[0].quota.weekly.remainingPercent.Known(),
            "list extra must never become quota and full/scoped must use one projection");
}

void TestAverageAndIdentity() {
    // Arrange / Act
    auto pool = Pool({Account(1, L"20"), Account(2, L"60", NOW, L"other-workspace")});
    auto summary = CalculateWeeklySummary(pool, NOW);
    // Assert
    Require(summary.percent == 60 && summary.usable == 2 && !summary.incomplete, "20/60 used must yield 60 remaining");

    // Arrange / Act
    pool = Pool({Account(9, L"90", NOW - 1), Account(3, L"20"), Account(2, L"60"),
                 Account(4, L"0", NOW, L"other-workspace")});
    summary = CalculateWeeklySummary(pool, NOW);
    // Assert
    Require(summary.percent == 70 && summary.usable == 2 && summary.expected == 2,
            "deduplicate full identity, freshest usable then smaller local ID");
    // A newer unusable record must not displace an older usable one.
    pool.accounts[2].quota.weekly.remainingPercent.state = FieldState::Invalid;
    Require(CalculateWeeklySummary(pool, NOW).percent == 90, "choose freshest usable snapshot");

    // Arrange / Act
    pool = Pool({Account(1, L"20"), Account(2, L"60", NOW, L"", L"user-fixture")});
    summary = CalculateWeeklySummary(pool, NOW);
    // Assert
    Require(summary.percent == 80 && summary.incomplete && summary.expected == 2 && !summary.expectedIsExact,
            "missing identity must not merge by email/name or become zero");
    pool.accounts[1].quota = ParseAccountQuota(Quota(L"60", NOW, L"workspace-fixture", L"other-user"), NOW);
    Require(CalculateWeeklySummary(pool, NOW).percent == 60, "same workspace with different user must remain separate");

    // Arrange / Act
    pool = Pool({Account(1, L"100"), Account(2, L"0", NOW, L"disabled"),
                 Account(3, L"0", NOW, L"error"), Account(4, L"0", NOW, L"manual")});
    pool.accounts[1].status.value = L"disabled";
    pool.accounts[2].status.value = L"error";
    pool.accounts[3].schedulable.value = false;
    summary = CalculateWeeklySummary(pool, NOW);
    // Assert
    Require(summary.percent == 0 && summary.usable == 1 && summary.expected == 1 && pool.accounts.size() == 4,
            "exhausted active subscription remains zero while disabled/error cards remain present");

    // Arrange / Act
    pool = Pool({Account(1, L"49.51"), Account(2, L"49.51", NOW, L"b"), Account(3, L"48.51", NOW, L"c")});
    // Assert
    Require(CalculateWeeklySummary(pool, NOW).percent == 51, "round only average of original fractions");
    for (const auto& test : {std::pair<PCWSTR, int>{L"49.5", 51}, {L"69.5", 31}, {L"39.5", 61}, {L"100", 0}, {L"0", 100}}) {
        Require(CalculateWeeklySummary(Pool({Account(1, test.first)}), NOW).percent == test.second,
                "half-up rounding or color boundary invalid");
    }
    Require(CalculateWeeklySummary(Pool({}), NOW).percent == -1, "empty pool has no percentage");
}

void TestAgeAndFailures() {
    // Arrange
    auto previous = Pool({Account(1)});
    auto failingQuota = [](const auto& path, auto& data, DWORD& status) {
        if (path.starts_with(L"/accounts?")) { data = Page(1, 1, Row(1)); return true; }
        status = 502; return false; // e.g. denied shadow parent: no bypass request.
    };
    // Act / Assert
    for (int age : {899, 900, 901}) {
        auto pool = LoadAccountPool(previous, failingQuota, NEVER_STOP, [=] { return NOW + age; });
        Require(!pool.fullySuccessful && pool.summary.incomplete &&
                pool.summary.percent == (age <= 900 ? 80 : -1), "cached values must expire by original timestamp");
        if (age <= 900) {
            Require(pool.accounts[0].quota.fetchedAt.value == NOW, "failure must not advance source clock");
        }
    }
    auto stale = LoadAccountPool(previous, [](const auto& path, auto& data, DWORD&) {
        if (path.starts_with(L"/accounts?")) { data = Page(1, 1, Row(1)); }
        else { data = Quota(L"20", NOW - 901); }
        return true;
    }, NEVER_STOP, CLOCK);
    Require(stale.summary.percent == -1 && !stale.fullySuccessful, "reread of old quota must not refresh age");

    // Arrange / Act
    auto revoked = LoadAccountPool(previous, [](const auto& path, auto& data, DWORD&) {
        if (path.starts_with(L"/accounts?")) { data = Page(1, 1, Row(1)); }
        else { data = Quota(L"20", NOW, L"", L""); }
        return true;
    }, NEVER_STOP, CLOCK);
    // Assert
    Require(revoked.summary.percent == -1 && revoked.summary.incomplete &&
            !HasSubscriptionIdentity(revoked.accounts[0].quota), "revoked identity must not be backfilled");

    // Arrange / Act: deadline is exhausted after list, before any quota starts.
    bool deadline = false;
    auto notStarted = LoadAccountPool(previous, [&](const auto&, auto& data, DWORD&) {
        data = Page(1, 1, Row(1)); deadline = true; return true;
    }, [&] { return deadline; }, CLOCK);
    // Assert
    Require(notStarted.summary.percent == 80 && notStarted.summary.incomplete &&
            !notStarted.fullySuccessful && notStarted.accounts[0].quotaRead == QuotaReadState::Failed,
            "deadline-skipped quota must retain only unexpired source data");

    // Arrange / Act
    auto listFailure = LoadAccountPool(previous, [](const auto&, auto&, DWORD& status) {
        status = 403; return false;
    }, NEVER_STOP, CLOCK);
    // Assert
    Require(listFailure.accounts.size() == 1 && !listFailure.accounts[0].discovered &&
            listFailure.summary.percent == 80 && listFailure.summary.incomplete,
            "failed list must retain unexpired previous cards as incomplete");

    // Arrange / Act
    auto pool = Pool({Account(1)});
    pool.accounts[0].quota = ParseAccountQuota(Json(L"{\"account_id\":\"w\",\"user_id\":\"u\","
        L"\"updated_at\":2000000000,\"rate_limit\":{\"primary_window\":" + Window(L"0", 604800) + L"}}"), NOW);
    // Assert
    Require(CalculateWeeklySummary(pool, NOW).percent == -1, "usage updated_at cannot replace quota fetched_at");
    pool.accounts[0].quota = ParseAccountQuota(Json(L"{\"account_id\":\"w\",\"user_id\":\"u\",\"fetched_at\":2000000000,"
        L"\"rate_limit\":{\"primary_window\":" + Window(L"0", 604800, NOW) + L"}}"), NOW);
    Require(CalculateWeeklySummary(pool, NOW).percent == -1, "expired window must not refill itself");
    pool.accounts[0].quota = ParseAccountQuota(Json(L"{\"account_id\":\"w\",\"user_id\":\"u\",\"fetched_at\":2000000000,"
        L"\"rate_limit\":{\"primary_window\":{\"used_percent\":20,\"limit_window_seconds\":604800,\"reset_after_seconds\":500}}}"), NOW);
    Require(CalculateWeeklySummary(pool, NOW).percent == 80 && !pool.accounts[0].quota.weekly.resetAt.Known(),
            "unknown absolute reset stays unknown; no synthetic HTTP reset timestamp");
    pool.accounts[0].quota = ParseAccountQuota(Quota(L"20", NOW + 1), NOW);
    Require(CalculateWeeklySummary(pool, NOW).percent == -1, "future source time is not fresh");
}

void TestWorkers() {
    // Arrange / Act
    std::atomic<int> supportedCalls{0};
    auto types = LoadAccountPool({}, [&](const auto& path, auto& data, DWORD&) {
        if (path.starts_with(L"/accounts?")) {
            data = Page(1, 4, Row(1) + L"," + Row(2, L"disabled") + L"," +
                Row(3, L"error") + L"," + Row(4, L"active", true, L"apikey"));
            return true;
        }
        supportedCalls++;
        data = Quota();
        return true;
    }, NEVER_STOP, CLOCK);
    // Assert
    Require(types.accounts.size() == 4 && supportedCalls == 3 && types.summary.usable == 1 &&
            types.accounts[3].quotaRead == QuotaReadState::NotSupported,
            "retain all statuses/types but query only OAuth rows");

    // Arrange
    std::mutex mutex;
    std::condition_variable condition;
    size_t active = 0, peak = 0, arrivals = 0, completed = 0;
    bool released = false;
    bool barrierTimedOut = false;
    AccountRequest request = [&](const auto& path, auto& data, DWORD& status) {
        if (path.starts_with(L"/accounts?")) { data = Page(1, 8, Rows(1, 8)); return true; }
        int id = std::stoi(path.substr(std::wstring(L"/openai/accounts/").size()));
        {
            std::unique_lock<std::mutex> lock(mutex);
            active++; arrivals++; peak = std::max(peak, active);
            if (arrivals >= QUOTA_CONCURRENCY) { released = true; condition.notify_all(); }
            if (!condition.wait_for(lock, std::chrono::seconds(5), [&] { return released; })) {
                barrierTimedOut = true;
                released = true;
                condition.notify_all();
            }
            active--; completed++;
        }
        if (id == 3) { status = 403; return false; }
        if (id == 4) { throw std::runtime_error("fixture request failure"); }
        auto identity = L"workspace-" + std::to_wstring(id);
        data = Quota(L"20", NOW, identity.c_str());
        return true;
    };
    // Act
    auto pool = LoadAccountPool({}, request, NEVER_STOP, CLOCK);
    // Assert
    Require(!barrierTimedOut && peak == QUOTA_CONCURRENCY && active == 0 && completed == 8,
            "bounded concurrent workers must all be joined");
    Require(pool.summary.usable == 6 && pool.summary.expected == 8 && pool.summary.incomplete &&
            !pool.fullySuccessful && pool.accounts[2].quotaRead == QuotaReadState::Failed,
            "quota partial failures must be linked by local ID");
    Require(pool.accounts[7].quota.upstreamAccountId.value == L"workspace-8", "quota must not follow completion order");

    // Arrange / Act
    std::atomic<bool> stopped{false};
    std::atomic<int> requested{0};
    auto cancelled = LoadAccountPool({}, [&](const auto& path, auto& data, DWORD&) {
        if (path.starts_with(L"/accounts?")) { data = Page(1, 8, Rows(1, 8)); return true; }
        requested++;
        stopped = true;
        data = Quota();
        return true;
    }, [&] { return stopped.load(); }, CLOCK);
    // Assert
    Require(cancelled.interrupted && !cancelled.fullySuccessful && requested <= QUOTA_CONCURRENCY,
            "cancellation must stop scheduling and join owners");
}

void TestPublication() {
    // Arrange
    g_dashboardCancelEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    Require(g_dashboardCancelEvent != nullptr, "fixture event creation failed");
    struct EventGuard {
        ~EventGuard() { CloseHandle(g_dashboardCancelEvent); g_dashboardCancelEvent = nullptr; }
    } eventGuard;
    g_dashboardUpdatesPaused = false;
    g_unloading = false;
    g_dashboardGeneration = 7;
    auto good = Pool({Account(1)});
    // Act / Assert
    Require(TryPublishAccountPool(good, 7, NOW, true) && g_dashboardText == L"80%" &&
            g_apiKeyRemainingPercent == 80 && g_dashboardAccounts.size() == 1 &&
            g_poolCountText == L"1/1 подп." && g_lastUpdatedText != L"—",
            "taskbar and popup must receive a coherent projection");
    auto lastSuccess = g_lastUpdatedText;
    auto partial = good;
    partial.quotaReadsSucceeded = false;
    partial.fullySuccessful = false;
    Require(TryPublishAccountPool(partial, 7, NOW, true) && g_dashboardText == L"80%*" &&
            g_lastUpdatedText == lastSuccess, "partial result must not advance success time");
    Require(SetEvent(g_dashboardCancelEvent), "fixture cancellation failed");
    Require(!TryPublishAccountPool(good, 7, NOW, true) && g_dashboardText == L"80%*",
            "cancelled response must not publish");
    Require(ResetEvent(g_dashboardCancelEvent), "fixture cancellation reset failed");
    g_dashboardGeneration++;
    Require(!TryPublishAccountPool(good, 7, NOW, true), "old settings generation must not publish after cancel reset");
    Require(TryPublishAccountPool(good, 8, NOW + 901, false) && g_apiKeyRemainingPercent == -1 &&
            !g_dashboardAccounts[0].dataQualityText.empty() && g_lastUpdatedText == lastSuccess,
            "age projection must expire percentage without inventing success");
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct ApartmentGuard { ~ApartmentGuard() { winrt::uninit_apartment(); } } apartment;
        TestPages();
        TestPagesWithoutCount();
        TestNormalization();
        TestAverageAndIdentity();
        TestAgeAndFailures();
        TestWorkers();
        TestPublication();
        std::cout << "Account data checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
    } catch (...) {
        std::cerr << "FAIL account data check exception\n";
    }
    return 1;
}
