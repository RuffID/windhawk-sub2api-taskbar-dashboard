// Isolated production-code fixtures. No HTTP, UI host or persistent user cache.
#ifndef WH_EDITING
#define WH_EDITING
#endif

#include "../src/versions/releases.h"
#include "../src/versions/presentation.h"
#include "../src/accounts/parser.h"
#include "../src/accounts/presentation.h"
#include "../src/ui/rendering/popup.h"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <exception>

using namespace sub2api_dashboard;
constexpr ULONGLONG NOW = 2000000000;

void Require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

wdj::JsonObject Json(const std::wstring& text) {
    wdj::JsonObject result = nullptr;
    Require(wdj::JsonObject::TryParse(text, result), "fixture JSON invalid");
    return result;
}

void TestReleaseSelection() {
    // Arrange
    std::wstring latest;
    const auto releases = LR"([{"draft":false,"tag_name":"v1.2.3-beta.9","prerelease":true},
        {"draft":true,"tag_name":"v9.0.0"},{"draft":false,"tag_name":"v1.2.3-beta.10","prerelease":true},
        {"draft":false,"tag_name":"v1.2.2","prerelease":false}])";
    // Act
    auto status = FindLatestPublishedRelease(releases, latest);
    // Assert
    Require(status == ReleaseCheckStatus::Succeeded && latest == L"v1.2.3-beta.10", "numeric prerelease order and original tag");
    Require(IsNewerReleaseVersion(L"1.2.3", latest), "stable beats prerelease");
    Require(IsNewerReleaseVersion(L"1.3.0-beta.1", L"1.2.3"), "higher prerelease beats older stable");
    Require(!IsNewerReleaseVersion(L"1.2.3+other", L"v1.2.3+build"), "build metadata has no precedence");
    Require(!IsNewerReleaseVersion(L"1.2.3-beta.01", L"1.2.2"), "numeric leading zero rejected");
    Require(FindLatestPublishedRelease(LR"([{"draft":false,"tag_name":"v1.2.3"},{"draft":false,"tag_name":"v1.2.3-beta.10"}])", latest)
        == ReleaseCheckStatus::Succeeded && latest == L"v1.2.3", "stable selection independent of array order");
    Require(FindLatestPublishedRelease(L"[]", latest) == ReleaseCheckStatus::Unknown && latest.empty(), "empty release list stays unknown");
    Require(FindLatestPublishedRelease(L"html", latest) == ReleaseCheckStatus::Failed, "non JSON rejected");
    Require(FindLatestPublishedRelease(LR"([{"draft":"false","tag_name":"v1.2.3"}])", latest) == ReleaseCheckStatus::Failed, "malformed draft rejected");
    Require(FindLatestPublishedRelease(LR"([{"draft":false,"tag_name":"v1.2.3\u0000suffix"}])", latest) == ReleaseCheckStatus::Unknown, "embedded NUL cannot truncate tag");
}

void TestServerAndUiStates() {
    // Arrange
    int calls = 0;
    auto read = [&](const std::wstring& resource, wdj::JsonObject& data, DWORD& status) {
        calls++;
        status = 200;
        if (resource == L"/system/version") { data = Json(LR"({"version":"v1.2.3"})"); }
        else {
            Require(resource == L"/system/check-updates", "only read-only version resources");
            data = Json(LR"({"has_update":true,"current_version":"wrong-source"})");
        }
        return true;
    };
    // Act
    auto version = ReadServerVersion(read, [] { return false; });
    version.latest = L"v1.2.4-beta.10";
    version.releaseCheckStatus = ReleaseCheckStatus::Succeeded;
    auto badge = GetPopupReleaseBadge(version);
    // Assert
    Require(calls == 2 && version.current == L"v1.2.3" && version.latestUpdateAvailable, "actual server contracts");
    Require(ShouldShowLatestUpdateIndicator(version, -1), "server update remains visible with unknown account mean");
    auto githubOnly = version;
    githubOnly.latestUpdateAvailable = false;
    Require(!ShouldShowLatestUpdateIndicator(githubOnly, 80), "GitHub update cannot create server indicator");
    Require(badge.releaseUrl == L"https://github.com/Wei-Shaw/sub2api/releases/tag/v1.2.4-beta.10" &&
            badge.borderColor == RGB(106, 212, 124) && badge.textColor == POPUP_DEFAULT_TEXT_COLOR, "new release link and border only");
    version.latest = version.current;
    Require(GetPopupReleaseBadge(version).text == L"Нет новых версий", "equal release is current");
    version.latest = L"v1.2.2";
    Require(GetPopupReleaseBadge(version).text == L"Нет новых версий", "older published version is current");
    version.current = L"dev";
    Require(GetPopupReleaseBadge(version).text == L"Текущая версия неизвестна", "unknown current is not current");
    version.current = L"v1.2.3";
    std::vector<std::wstring> stateTexts;
    for (auto state : {ReleaseCheckStatus::Pending, ReleaseCheckStatus::Failed, ReleaseCheckStatus::RateLimited, ReleaseCheckStatus::Unknown}) {
        version.releaseCheckStatus = state;
        auto result = GetPopupReleaseBadge(version);
        Require(result.text != L"Нет новых версий" && result.releaseUrl.empty(), "non-success state cannot claim current or link stale tag");
        Require(std::find(stateTexts.begin(), stateTexts.end(), result.text) == stateTexts.end(), "waiting, error, unknown and rate limit stay distinct");
        stateTexts.push_back(result.text);
        if (state == ReleaseCheckStatus::Failed || state == ReleaseCheckStatus::RateLimited) {
            Require(result.borderColor == RGB(242, 113, 113) && result.textColor == POPUP_DEFAULT_TEXT_COLOR,
                "failure changes border, not text color");
        }
    }
    Require(GetPopupReleaseBadge(DashboardVersion{}).text == L"Ожидание проверки GitHub", "initial state waits");
    auto failure = ReadServerVersion([](const std::wstring&, wdj::JsonObject&, DWORD& status) {
        status = 403;
        return false;
    }, [] { return false; });
    Require(failure.currentStatus == ReleaseCheckStatus::Failed && !ShouldShowLatestUpdateIndicator(failure, 80), "403 is failed, not guessed from key prefix");
    auto limited = ReadServerVersion([](const std::wstring&, wdj::JsonObject&, DWORD& status) {
        status = 429;
        return false;
    }, [] { return false; });
    Require(limited.currentStatus == ReleaseCheckStatus::RateLimited && limited.serverCheckStatus == ReleaseCheckStatus::RateLimited, "server rate limit visible");
    auto partial = ReadServerVersion([](const std::wstring& resource, wdj::JsonObject& data, DWORD& status) {
        status = 200;
        if (resource == L"/system/version") { data = Json(LR"({"current_version":"wrong-contract"})"); }
        else { data = Json(LR"({"has_update":true})"); }
        return true;
    }, [] { return false; });
    Require(partial.currentStatus == ReleaseCheckStatus::Failed && partial.serverCheckStatus == ReleaseCheckStatus::Succeeded &&
        ShouldShowLatestUpdateIndicator(partial, -1), "one failed resource does not discard successful server update flag");
    bool available = true;
    Require(!ParseServerUpdate(Json(LR"({"has_update":false,"warning":"fixture warning"})"), available) && !available, "upstream warning is not current");
    Require(!ParseServerUpdate(Json(LR"({"has_update":"false"})"), available), "string boolean rejected");
    std::wstring current;
    Require(!ParseServerVersion(Json(LR"({"version":null})"), current), "null current rejected");
    Require(ParseServerVersion(Json(LR"({"version":""})"), current) && current.empty(), "empty current stays unknown");
    calls = 0;
    ReadServerVersion(read, [] { return true; });
    Require(calls == 0, "cancelled server read makes no requests");
    Require(ReleaseTagUrl(L"v1.2.3+build/space ?#%") ==
        L"https://github.com/Wei-Shaw/sub2api/releases/tag/v1.2.3%2Bbuild%2Fspace%20%3F%23%25", "tag is one URL path segment");
    Require(std::wstring(MOD_GITHUB_URL).empty(), "unknown mod URL stays disabled");
}

void TestRateLimits() {
    // Arrange
    HttpResponse response;
    response.status = 403;
    // Act / Assert
    Require(!IsGitHubRateLimited(response), "ordinary 403 is failure");
    response.body = LR"({"message":"API rate limit exceeded"})";
    Require(IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, NOW) == NOW + 3600, "403 rate limit fallback");
    response.status = 429;
    response.retryAfter = L"7200";
    response.rateLimitReset = std::to_wstring(NOW + 8000);
    Require(GetGitHubNextCheckUtc(response, NOW) == NOW + 8001, "later reset wins over retry");
    response.retryAfter = L"9000";
    Require(GetGitHubNextCheckUtc(response, NOW) == NOW + 9000, "later retry wins over reset");
    response.retryAfter = L"999999999999999999999999999999";
    response.rateLimitReset.clear();
    Require(GetGitHubNextCheckUtc(response, NOW) == NOW + 3600, "overflow is not HTTP date");
    response.retryAfter = L"Wed, 18 May 2033 06:20:00 GMT"; // NOW + 10000
    Require(GetGitHubNextCheckUtc(response, NOW) == NOW + 10001, "HTTP date retry supported");
    response.status = 200;
    response.body.clear();
    response.retryAfter.clear();
    response.rateLimitRemaining = L"0";
    response.rateLimitReset = std::to_wstring(NOW + 8000);
    Require(!IsGitHubRateLimited(response) && GetGitHubNextCheckUtc(response, NOW) == NOW + 8001, "successful last request keeps success with deferred retry");
}

struct TemporaryCache {
    std::wstring path;
    TemporaryCache() {
        wchar_t folder[MAX_PATH], file[MAX_PATH];
        Require(GetTempPathW(MAX_PATH, folder) != 0, "temporary directory unavailable");
        Require(GetTempFileNameW(folder, L"s2v", 0, file) != 0, "temporary file unavailable");
        path = file;
        Require(DeleteFileW(path.c_str()) != FALSE, "temporary file preparation failed");
    }
    ~TemporaryCache() {
        DeleteFileW(path.c_str());
        DeleteFileW((path + L".tmp").c_str());
    }
};

void TestCacheAndClients() {
    // Arrange
    TemporaryCache temporary;
    EventHandle cancel;
    std::atomic<int> requests = 0;
    auto request = [&](HttpResponse& response) {
        requests++;
        response.status = 200;
        response.body = LR"([{"draft":false,"tag_name":"v1.2.4-beta.10"}])";
        return true;
    };
    auto stop = [] { return false; };
    auto clock = [] { return NOW; };
    auto client = [&] {
        SharedReleaseCache cache;
        Require(cache.Open(temporary.path, cancel.handle), "client lock unavailable");
        return CheckPublishedRelease(cache, request, stop, clock);
    };
    // Act
    auto first = client();
    auto reopened = client();
    // Assert
    Require(requests == 1 && reopened.latest == first.latest && reopened.status == ReleaseCheckStatus::Succeeded, "reload and manual refresh share persistent minimum");
    Require(GetReleaseCacheMutexName(temporary.path).starts_with(L"Global\\Sub2ApiTaskbarDashboard."), "independent mutex namespace");

    // Arrange: expire the slot and run two concurrent clients on one empty cache.
    {
        SharedReleaseCache cache;
        Require(cache.Open(temporary.path, cancel.handle), "cache lock unavailable");
        cache.Write({});
    }
    std::exception_ptr leftError, rightError;
    auto threadClient = [&](std::exception_ptr& error) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
            client();
        } catch (...) { error = std::current_exception(); }
    };
    // Act
    std::thread left([&] { threadClient(leftError); });
    struct Join { std::thread& thread; ~Join() { if (thread.joinable()) { thread.join(); } } } join{left};
    std::thread right([&] { threadClient(rightError); });
    right.join();
    left.join();
    // Assert
    Require(!leftError && !rightError && requests == 2, "two clients perform exactly one additional request");
    {
        SharedReleaseCache cache;
        Require(cache.Open(temporary.path, cancel.handle), "cache lock unavailable");
        cache.Write({});
        auto cancelled = CheckPublishedRelease(cache, [&](HttpResponse&) {
            Require(SetEvent(cancel.handle) != FALSE, "cancellation failed");
            return false;
        }, [&] { return WaitForSingleObject(cancel.handle, 0) != WAIT_TIMEOUT; }, clock);
        Require(cancelled.status == ReleaseCheckStatus::Failed && cache.Read().status == ReleaseCheckStatus::Failed &&
            cancelled.nextCheckUtc >= NOW + 3600, "cancellation settles reservation without bypassing minimum");
    }
    {
        SharedReleaseCache blocked;
        Require(!blocked.Open(temporary.path, cancel.handle), "cancelled lock wait stops");
    }
    Require(ResetEvent(cancel.handle) != FALSE, "cancellation reset failed");
    {
        SharedReleaseCache cache;
        Require(cache.Open(temporary.path, cancel.handle), "mutex released after cancellation");
        cache.Write({});
        auto failure = CheckPublishedRelease(cache, [](HttpResponse&) -> bool { throw std::runtime_error("fixture"); }, stop, clock);
        Require(failure.status == ReleaseCheckStatus::Failed && cache.Read().status == ReleaseCheckStatus::Failed, "exception settles reservation");
        cache.Write({});
        auto limited = CheckPublishedRelease(cache, [](HttpResponse& response) {
            response.status = 429;
            response.retryAfter = L"7200";
            return true;
        }, stop, clock);
        Require(limited.status == ReleaseCheckStatus::RateLimited && cache.Read().nextCheckUtc == NOW + 7200, "rate limit persisted");
        CheckPublishedRelease(cache, request, stop, [] { return NOW + 3600; });
        Require(requests == 2, "manual request cannot bypass retry after");
        cache.Write({});
        auto forbidden = CheckPublishedRelease(cache, [](HttpResponse& response) {
            response.status = 403;
            return true;
        }, stop, clock);
        Require(forbidden.status == ReleaseCheckStatus::Failed && cache.Read().nextCheckUtc >= NOW + 3600,
            "ordinary 403 persists failure with hourly pause");
        cache.Write({NOW + 1, ReleaseCheckStatus::Succeeded, L"invalid"});
        bool rejected = false;
        try { CheckPublishedRelease(cache, request, stop, clock); }
        catch (...) { rejected = true; }
        Require(rejected && requests == 2, "corrupt cache fails closed before network request");
    }
}

void TestAccountPresentationAndPublication() {
    // Arrange: monthly is visible but cannot become a weekly mean.
    AccountPoolSnapshot pool;
    auto row = Json(LR"({"id":1,"platform":"openai","type":"oauth","status":"active","schedulable":true,"name":"Fixture"})");
    AccountPage page;
    Require(ParseAccountPage(Json(L"{\"total\":1,\"page\":1,\"page_size\":100,\"pages\":1,\"items\":[" +
        std::wstring(row.Stringify()) + L"]}"), 1, page) && page.accounts.size() == 1, "account fixture invalid");
    AccountSnapshot account = page.accounts[0];
    account.quota = ParseAccountQuota(Json(LR"({"fetched_at":2000000000,"account_id":"fixture-account","user_id":"fixture-user",
        "rate_limit":{"secondary_window":{"used_percent":20,"limit_window_seconds":2592000}}})"), NOW);
    account.quotaRead = QuotaReadState::Succeeded;
    pool.accounts.push_back(account);
    // Act
    auto cards = MakeDashboardAccounts(pool, NOW + 901);
    // Assert
    Require(cards.size() == 1 && !cards[0].hasPrimary && cards[0].secondaryIsMonthly && cards[0].secondaryPercent == 80,
        "monthly fills long slot when 5h absent");
    Require(CalculateWeeklySummary(pool, NOW).percent == -1, "monthly never substitutes weekly");
    Require(cards[0].dataQualityText.find(L"Возраст:") != std::wstring::npos && cards[0].dataQualityText.find(L"Устарело") != std::wstring::npos,
        "old card percentage has explicit age and stale state");
    Require(cards[0].availableResetCredits == -1 && cards[0].secondaryResetText.empty(), "unknown reset count and time stay unknown");
    auto columns = GetPopupLimitColumns(cards[0], RECT{0, 0, 338, 116});
    Require(columns.width == 322 && columns.left == columns.right, "long-only window uses full card width");
    auto card = cards[0];
    Require(GetResetCreditBadge(card).text.empty(), "unknown Reset badge hidden");
    card.availableResetCredits = 0;
    Require(GetResetCreditBadge(card).text.empty(), "zero Reset badge hidden");
    card.availableResetCredits = 3;
    auto resetBadge = GetResetCreditBadge(card);
    Require(resetBadge.text == L"Reset (3)" && resetBadge.expiryText.empty(), "unknown expiry hides countdown only");
    bool soon = false;
    constexpr ULONGLONG sevenDays = 7ULL * 24 * 60 * 60 * 10000000;
    Require(FormatResetCreditExpiryTicks(sevenDays, soon) == L"7d" && soon, "exactly seven days is red");
    FormatResetCreditExpiryTicks(sevenDays + 1, soon);
    Require(!soon, "more than seven days is normal");
    card.hasPrimary = true;
    columns = GetPopupLimitColumns(card, RECT{0, 0, 338, 116});
    Require(columns.width == 157 && columns.right == columns.left + columns.width + 8, "two windows retain gap");

    // Arrange: both long windows are retained, without creating a five-hour slot.
    auto both = account;
    both.quota = ParseAccountQuota(Json(LR"({"fetched_at":2000000000,"email":"quota@example.invalid",
        "rate_limit":{"primary_window":{"used_percent":30,"limit_window_seconds":604800},
        "secondary_window":{"used_percent":40,"limit_window_seconds":2592000}}})"), NOW);
    pool.accounts = {both};
    // Act
    auto bothCards = MakeDashboardAccounts(pool, NOW);
    // Assert
    Require(bothCards[0].hasPrimary && bothCards[0].primaryLabel == L"Monthly" && bothCards[0].primaryPercent == 60 &&
        bothCards[0].secondaryPercent == 70 && !bothCards[0].secondaryIsMonthly && bothCards[0].email == L"quota@example.invalid",
        "both long windows and permitted identity are projected without fake 5h");
    both.quota = ParseAccountQuota(Json(LR"({"fetched_at":2000000000,
        "rate_limit":{"primary_window":{"used_percent":20,"limit_window_seconds":18000},
        "secondary_window":{"used_percent":30,"limit_window_seconds":604800}}})"), NOW);
    pool.accounts = {both};
    auto fiveHourCards = MakeDashboardAccounts(pool, NOW);
    Require(fiveHourCards[0].hasPrimary && fiveHourCards[0].primaryLabel == L"5h" && fiveHourCards[0].primaryPercent == 80,
        "available 5h remains separate");

    // Arrange: fractional order must survive equal rounded percentages.
    auto second = account;
    account.localId = 2;
    account.quota.monthly.remainingPercent.value = 1.4;
    second.quota.monthly.remainingPercent.value = 1.2;
    pool.accounts = {account, second, AccountSnapshot{}};
    // Act
    cards = MakeDashboardAccounts(pool, NOW);
    // Assert
    Require(cards[0].alias == L"Fixture" && cards[0].secondarySortPercent == 1.2 && cards[1].secondarySortPercent == 1.4 &&
        cards[2].secondaryPercent == -1, "unrounded known limits sort first");
    Require(GetPopupMaxScroll(4, 340) == 0 && GetPopupMaxScroll(6, 340) > 0 && ScalePopupLength(124, 144) == 186,
        "four cards fit, later cards scroll, DPI scale retained");

    // Arrange: publication must not touch account success, payload or age.
    EventHandle cancel;
    g_dashboardCancelEvent = cancel.handle;
    g_dashboardUpdatesPaused = false;
    g_unloading = false;
    g_dashboardGeneration = 4;
    g_lastUpdatedText = L"fixture-success";
    g_dashboardAccounts = cards;
    g_dashboardText = L"80%*";
    DashboardVersion version;
    version.current = L"v1.2.3";
    // Act / Assert
    Require(TryPublishVersion(version, 4) && g_dashboardText == L"80%*" && g_dashboardAccounts.size() == cards.size() &&
        g_lastUpdatedText == L"fixture-success", "versions cannot replace account result or success time");
    version.current = L"old-result";
    Require(!TryPublishVersion(version, 3) && g_dashboardVersion.current == L"v1.2.3", "old settings cannot publish");
    Require(SetEvent(cancel.handle) != FALSE, "cancel event failed");
    Require(!TryPublishVersion(version, 4), "cancelled version cannot publish");
    g_dashboardCancelEvent = nullptr;
}

int main() {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct Apartment { ~Apartment() { winrt::uninit_apartment(); } } apartment;
        TestReleaseSelection();
        TestServerAndUiStates();
        TestRateLimits();
        TestCacheAndClients();
        TestAccountPresentationAndPublication();
        std::cout << "Popup and version checks passed\n";
        return 0;
    } catch (...) {
        std::cerr << "FAIL popup/version fixture\n";
        return 1;
    }
}
