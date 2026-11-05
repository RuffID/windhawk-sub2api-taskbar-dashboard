#pragma once

#include "release_cache.h"
#include "../network/http.h"
#include "../core/logging.h"

namespace sub2api_dashboard {

bool ReadVersionString(const wdj::JsonObject& data, PCWSTR name, std::wstring& value) {
    wdj::IJsonValue field = nullptr;
    if (!TryGetJsonValue(data, name, field) || field.ValueType() != wdj::JsonValueType::String) { return false; }
    value = std::wstring(field.GetString());
    return true;
}

// Preserve the server's spelling (including v/build metadata) for display/links.
bool ParseServerVersion(const wdj::JsonObject& data, std::wstring& current) {
    current.clear();
    if (!ReadVersionString(data, L"version", current)) { return false; }
    current = TrimString(current);
    if (current.size() > 256 || std::any_of(current.begin(), current.end(),
        [](wchar_t ch) { return ch < L' ' || ch == 127; })) {
        current.clear();
        return false;
    }
    return true;
}

bool ParseServerUpdate(const wdj::JsonObject& data, bool& available) {
    available = false;
    bool parsed = false;
    if (!TryGetJsonBoolean(data, L"has_update", parsed)) { return false; }
    // The server returns HTTP 200 with warning when its upstream check fails.
    if (data.HasKey(L"warning")) {
        std::wstring warning;
        if (!ReadVersionString(data, L"warning", warning) || !warning.empty()) { return false; }
    }
    available = parsed;
    return true;
}

ReleaseCheckStatus FindLatestPublishedRelease(const std::wstring& body, std::wstring& latest) {
    latest.clear();
    wdj::JsonArray releases = nullptr;
    if (!wdj::JsonArray::TryParse(body, releases)) { return ReleaseCheckStatus::Failed; }
    ReleaseVersion newest;
    bool found = false;
    for (uint32_t i = 0; i < releases.Size(); i++) {
        auto item = releases.GetAt(i);
        if (item.ValueType() != wdj::JsonValueType::Object) { return ReleaseCheckStatus::Failed; }
        auto release = item.GetObject();
        bool draft = false;
        if (!TryGetJsonBoolean(release, L"draft", draft)) { return ReleaseCheckStatus::Failed; }
        if (draft) { continue; }
        std::wstring tag;
        if (!ReadVersionString(release, L"tag_name", tag)) { return ReleaseCheckStatus::Failed; }
        ReleaseVersion candidate;
        if (tag.size() > 256 || !ParseReleaseVersion(tag, candidate)) { continue; }
        if (!found || CompareReleaseVersions(candidate, newest) > 0) {
            newest = std::move(candidate);
            latest = tag;
            found = true;
        }
    }
    if (found) { return ReleaseCheckStatus::Succeeded; }
    return ReleaseCheckStatus::Unknown;
}

bool TryParseReleaseSeconds(const std::wstring& value, ULONGLONG& result) {
    std::wstring text = TrimString(value);
    if (text.empty()) { return false; }
    ULONGLONG parsed = 0;
    for (wchar_t ch : text) {
        if (ch < L'0' || ch > L'9' || parsed > (ULLONG_MAX - (ch - L'0')) / 10) { return false; }
        parsed = parsed * 10 + (ch - L'0');
    }
    result = parsed;
    return true;
}

bool IsGitHubRateLimited(const HttpResponse& response) {
    if (response.status == 429) { return true; }
    if (response.status != 403) { return false; }
    if (TrimString(response.rateLimitRemaining) == L"0" || !TrimString(response.retryAfter).empty()) {
        return true;
    }
    wdj::JsonObject error = nullptr;
    std::wstring message;
    if (!wdj::JsonObject::TryParse(response.body, error) ||
        !TryGetJsonString(error, L"message", message)) { return false; }
    std::transform(message.begin(), message.end(), message.begin(), [](wchar_t ch) {
        if (ch >= L'A' && ch <= L'Z') { return static_cast<wchar_t>(ch + L'a' - L'A'); }
        return ch;
    });
    return message.find(L"rate limit") != std::wstring::npos ||
           message.find(L"abuse detection") != std::wstring::npos;
}

ULONGLONG GetGitHubNextCheckUtc(const HttpResponse& response, ULONGLONG now) {
    ULONGLONG next = now + RELEASE_CHECK_INTERVAL_SECONDS;
    constexpr ULONGLONG maxTimestamp = 253402300799ULL;
    ULONGLONG reset = 0;
    if ((IsGitHubRateLimited(response) || TrimString(response.rateLimitRemaining) == L"0") &&
        TryParseReleaseSeconds(response.rateLimitReset, reset) && reset > now && reset < maxTimestamp) {
        next = std::max(next, reset + 1);
    }
    ULONGLONG retry = 0;
    if (TryParseReleaseSeconds(response.retryAfter, retry) && retry <= maxTimestamp - now) {
        next = std::max(next, now + retry);
    } else if (response.retryAfter.find(L',') != std::wstring::npos && response.retryAfter.ends_with(L" GMT")) {
        SYSTEMTIME time{};
        FILETIME fileTime{};
        if (WinHttpTimeToSystemTime(response.retryAfter.c_str(), &time) && SystemTimeToFileTime(&time, &fileTime)) {
            ULARGE_INTEGER ticks{};
            ticks.LowPart = fileTime.dwLowDateTime;
            ticks.HighPart = fileTime.dwHighDateTime;
            if (ticks.QuadPart >= 116444736000000000ULL) {
                auto deadline = (ticks.QuadPart - 116444736000000000ULL) / 10000000ULL;
                if (deadline > now && deadline < maxTimestamp) { next = std::max(next, deadline + 1); }
            }
        }
    }
    return next;
}

// The caller owns the interprocess lock through read/reserve/request/settle.
// Persisting the slot first preserves the hourly minimum even after a crash.
// Every normal exit settles Pending, including cancellation and request errors.
template <typename Request, typename Stop, typename Clock>
ReleaseCacheData CheckPublishedRelease(SharedReleaseCache& cache, Request request, Stop stop, Clock clock) {
    auto data = cache.Read();
    if (stop() || !ReserveReleaseCheck(data, clock())) { return data; }
    cache.Write(data);
    HttpResponse response;
    try {
        bool received = !stop() && request(response);
        data.status = ReleaseCheckStatus::Failed;
        if (received && !stop() && response.status >= 200 && response.status < 300) {
            data.status = FindLatestPublishedRelease(response.body, data.latest);
        } else if (IsGitHubRateLimited(response)) {
            data.status = ReleaseCheckStatus::RateLimited;
        }
    } catch (...) {
        data.status = ReleaseCheckStatus::Failed;
        Wh_Log(L"GitHub release request failed: hr=%08X", CurrentExceptionHResult());
    }
    if (data.status != ReleaseCheckStatus::Succeeded) { data.latest.clear(); }
    data.nextCheckUtc = std::max(data.nextCheckUtc, GetGitHubNextCheckUtc(response, clock()));
    cache.Write(data);
    if (!stop() && data.status != ReleaseCheckStatus::Succeeded) {
        Wh_Log(L"GitHub release check incomplete: status=%u state=%d", response.status, static_cast<int>(data.status));
    }
    return data;
}

bool TryPublishVersion(const DashboardVersion& version, uint64_t generation) {
    std::lock_guard<std::mutex> lock(g_dashboardMutex);
    if (IsDashboardRefreshCancelled() || generation != g_dashboardGeneration) { return false; }
    g_dashboardVersion = version;
    return true;
}

// Independent calls keep one failed resource from discarding the other result.
template <typename Request, typename Stop>
DashboardVersion ReadServerVersion(Request request, Stop stop) {
    DashboardVersion version;
    version.currentStatus = ReleaseCheckStatus::Failed;
    version.serverCheckStatus = ReleaseCheckStatus::Failed;
    wdj::JsonObject data = nullptr;
    DWORD status = 0;
    try {
        if (!stop() && request(L"/system/version", data, status) && ParseServerVersion(data, version.current)) {
            version.currentStatus = version.current.empty() ? ReleaseCheckStatus::Unknown : ReleaseCheckStatus::Succeeded;
        } else if (status == 429) { version.currentStatus = ReleaseCheckStatus::RateLimited; }
    } catch (...) { Wh_Log(L"Server version failed: hr=%08X", CurrentExceptionHResult()); }
    if (!stop() && version.currentStatus == ReleaseCheckStatus::Failed) {
        Wh_Log(L"Server version unavailable: status=%u", status);
    }
    data = nullptr;
    status = 0;
    try {
        if (!stop() && request(L"/system/check-updates", data, status) && ParseServerUpdate(data, version.latestUpdateAvailable)) {
            version.serverCheckStatus = ReleaseCheckStatus::Succeeded;
        } else if (status == 429) { version.serverCheckStatus = ReleaseCheckStatus::RateLimited; }
    } catch (...) { Wh_Log(L"Server update check failed: hr=%08X", CurrentExceptionHResult()); }
    if (!stop() && version.serverCheckStatus == ReleaseCheckStatus::Failed) {
        Wh_Log(L"Server update check unavailable: status=%u", status);
    }
    return version;
}

}  // namespace sub2api_dashboard
