#pragma once

#include "../core/state.h"
#include "../core/text_json.h"
#include "../config/settings.h"

namespace sub2api_dashboard {

struct WinHttpHandle {
    HINTERNET handle = nullptr;
    HANDLE closingEvent = nullptr;

    WinHttpHandle() = default;

    explicit WinHttpHandle(HINTERNET handle) : handle(handle) {}

    ~WinHttpHandle() {
        if (handle) {
            WinHttpCloseHandle(handle);
            if (closingEvent) {
                // The async context and read buffer must outlive the final callback.
                WaitForSingleObject(closingEvent, INFINITE);
            }
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    explicit operator bool() const {
        return handle != nullptr;
    }
};

struct EventHandle {
    HANDLE handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    EventHandle() {
        if (!handle) {
            winrt::throw_last_error();
        }
    }

    ~EventHandle() {
        CloseHandle(handle);
    }

    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;
};

bool IsDashboardRefreshCancelled() {
    return g_unloading || g_dashboardUpdatesPaused ||
           !g_dashboardCancelEvent ||
           WaitForSingleObject(g_dashboardCancelEvent, 0) != WAIT_TIMEOUT;
}

struct AsyncHttpState {
    EventHandle completed;
    EventHandle closed;
    std::atomic<DWORD> status;
    std::atomic<DWORD> error;
    std::atomic<DWORD> bytesRead;
    char readBuffer[8192];

    bool Prepare(ULONGLONG deadline) {
        if (IsDashboardRefreshCancelled() || GetTickCount64() >= deadline) {
            return false;
        }
        status = 0;
        error = 0;
        bytesRead = 0;
        if (!ResetEvent(completed.handle)) {
            winrt::throw_last_error();
        }
        return true;
    }

    bool Wait(DWORD expectedStatus, ULONGLONG deadline) {
        ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            Wh_Log(L"WinHTTP request deadline exceeded");
            return false;
        }
        HANDLE events[]{g_dashboardCancelEvent, completed.handle};
        DWORD result = WaitForMultipleObjects(
            ARRAYSIZE(events), events, FALSE, static_cast<DWORD>(deadline - now));
        if (result == WAIT_OBJECT_0) {
            return false;
        }
        if (result != WAIT_OBJECT_0 + 1) {
            Wh_Log(L"WinHTTP wait failed or timed out: %u", result);
            return false;
        }
        if (status != expectedStatus) {
            Wh_Log(L"WinHTTP async operation failed: status=%u error=%u",
                   status.load(), error.load());
            return false;
        }
        return !IsDashboardRefreshCancelled() && GetTickCount64() < deadline;
    }
};

void CALLBACK HttpStatusCallback(HINTERNET,
                                DWORD_PTR context,
                                DWORD status,
                                LPVOID information,
                                DWORD informationLength) noexcept {
    auto* state = reinterpret_cast<AsyncHttpState*>(context);
    if (!state) {
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        SetEvent(state->closed.handle);
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        state->error = static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
    } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state->bytesRead = informationLength;
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
               status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        return;
    }
    state->status = status;
    SetEvent(state->completed.handle);
}

struct HttpResponse {
    DWORD status = 0;
    std::wstring body;
    std::wstring rateLimitRemaining;
    std::wstring rateLimitReset;
    std::wstring retryAfter;
};

struct HttpGetRequest {
    std::wstring url;
    std::wstring headers;
    bool collectRateLimitHeaders = false;
};

HttpGetRequest MakeSub2ApiRequest(const Settings& settings,
                                 const std::wstring& resource) {
    return {settings.sub2apiUrl + L"/api/v1/admin" + resource,
            L"Accept: application/json\r\nx-api-key: " + settings.adminApiKey + L"\r\n",
            false};
}

bool MakeGitHubReleaseRequest(const std::wstring& githubToken,
                              HttpGetRequest& request) {
    request = {};
    if (!IsValidHeaderCredential(githubToken)) { return false; }
    request.url = L"https://api.github.com/repos/Wei-Shaw/sub2api/releases?per_page=100";
    request.headers = L"Accept: application/vnd.github+json\r\n";
    request.collectRateLimitHeaders = true;
    if (!githubToken.empty()) {
        request.headers += L"Authorization: Bearer " + githubToken + L"\r\n";
    }
    return true;
}

std::wstring QueryOptionalHttpHeader(HINTERNET request, PCWSTR name) {
    DWORD size = 0;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, nullptr,
                             &size, WINHTTP_NO_HEADER_INDEX)) {
        DWORD error = GetLastError();
        if (error == ERROR_WINHTTP_HEADER_NOT_FOUND) {
            return {};
        }
        if (error != ERROR_INSUFFICIENT_BUFFER) {
            winrt::throw_hresult(HRESULT_FROM_WIN32(error));
        }
    }
    if (!size) {
        return {};
    }
    if (size > 16384) {
        winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(),
                             &size, WINHTTP_NO_HEADER_INDEX)) {
        winrt::throw_last_error();
    }
    value.resize(size / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

// Only the two destination-specific wrappers below construct authenticated requests.
namespace http_detail {

constexpr size_t MAX_RESPONSE_BYTES = 4 * 1024 * 1024;

bool HttpGet(const HttpGetRequest& target,
             DWORD timeoutSeconds,
             ULONGLONG refreshDeadline,
             HttpResponse& response) {
    response = {};
    if (IsDashboardRefreshCancelled()) {
        return false;
    }
    ULONGLONG now = GetTickCount64();
    ULONGLONG deadline = std::min(
        refreshDeadline, now + static_cast<ULONGLONG>(timeoutSeconds) * 1000);
    if (now >= deadline) {
        Wh_Log(L"HTTP refresh deadline exceeded");
        return false;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(target.url.c_str(), 0, 0, &components)) {
        Wh_Log(L"WinHttpCrackUrl failed: %u", GetLastError());
        return false;
    }

    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring requestPath(components.lpszUrlPath,
                             components.dwUrlPathLength);
    if (components.dwExtraInfoLength) {
        requestPath.append(components.lpszExtraInfo,
                           components.dwExtraInfoLength);
    }

    WinHttpHandle session(WinHttpOpen(L"Sub2ApiTaskbarDashboard/0.1",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session) {
        Wh_Log(L"WinHttpOpen failed: %u", GetLastError());
        return false;
    }

    int timeoutMs =
        static_cast<int>(std::min<ULONGLONG>(deadline - now, INT_MAX));
    if (!WinHttpSetTimeouts(session.handle, timeoutMs, timeoutMs, timeoutMs,
                           timeoutMs)) {
        Wh_Log(L"WinHttpSetTimeouts failed: %u", GetLastError());
        return false;
    }

    WinHttpHandle connect(
        WinHttpConnect(session.handle, host.c_str(), components.nPort, 0));
    if (!connect) {
        Wh_Log(L"WinHttpConnect failed: %u", GetLastError());
        return false;
    }

    DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS
                      ? WINHTTP_FLAG_SECURE
                      : 0;
    AsyncHttpState asyncState;
    WinHttpHandle request(WinHttpOpenRequest(
        connect.handle, L"GET", requestPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) {
        Wh_Log(L"WinHttpOpenRequest failed: %u", GetLastError());
        return false;
    }
    DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&asyncState);
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_CONTEXT_VALUE,
                          &context, sizeof(context))) {
        Wh_Log(L"WinHttpSetOption context failed: %u", GetLastError());
        return false;
    }
    if (WinHttpSetStatusCallback(
            request.handle, HttpStatusCallback,
            WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
                WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
                WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
                WINHTTP_CALLBACK_FLAG_HANDLES,
            0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        Wh_Log(L"WinHttpSetStatusCallback failed: %u", GetLastError());
        return false;
    }
    request.closingEvent = asyncState.closed.handle;

    // Never forward a key through redirects, cookie storage or negotiated login.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    DWORD disabledFeatures = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirectPolicy, sizeof(redirectPolicy)) ||
        !WinHttpSetOption(request.handle, WINHTTP_OPTION_DISABLE_FEATURE,
                          &disabledFeatures, sizeof(disabledFeatures))) {
        Wh_Log(L"WinHTTP security options failed: %u", GetLastError());
        return false;
    }
    if (!asyncState.Prepare(deadline)) {
        return false;
    }
    BOOL sent = WinHttpSendRequest(
        request.handle, target.headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                               : target.headers.c_str(),
        target.headers.empty() ? 0 : static_cast<DWORD>(-1),
        WINHTTP_NO_REQUEST_DATA, 0, 0, context);
    if (!sent) {
        Wh_Log(L"WinHttp request failed: %u", GetLastError());
        return false;
    }
    if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline) ||
        !asyncState.Prepare(deadline)) {
        return false;
    }
    if (!WinHttpReceiveResponse(request.handle, nullptr)) {
        Wh_Log(L"WinHttpReceiveResponse failed: %u", GetLastError());
        return false;
    }
    if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline)) {
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.handle,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        Wh_Log(L"WinHttpQueryHeaders status failed: %u", GetLastError());
        return false;
    }

    response.status = status;
    if (target.collectRateLimitHeaders) {
        response.rateLimitRemaining = QueryOptionalHttpHeader(request.handle, L"X-RateLimit-Remaining");
        response.rateLimitReset = QueryOptionalHttpHeader(request.handle, L"X-RateLimit-Reset");
        response.retryAfter = QueryOptionalHttpHeader(request.handle, L"Retry-After");
    }

    std::string responseBytes;
    for (;;) {
        if (!asyncState.Prepare(deadline)) {
            return false;
        }
        if (!WinHttpReadData(request.handle, asyncState.readBuffer,
                             sizeof(asyncState.readBuffer), nullptr)) {
            Wh_Log(L"WinHttpReadData failed: %u", GetLastError());
            return false;
        }
        if (!asyncState.Wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline)) {
            return false;
        }
        DWORD read = asyncState.bytesRead;
        if (read == 0) {
            break;
        }
        if (read > MAX_RESPONSE_BYTES - responseBytes.size()) {
            Wh_Log(L"HTTP response size limit exceeded");
            return false;
        }
        responseBytes.append(asyncState.readBuffer, read);
    }

    if (IsDashboardRefreshCancelled() || GetTickCount64() >= deadline) {
        return false;
    }
    if (!responseBytes.empty()) {
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        responseBytes.data(),
                                        static_cast<int>(responseBytes.size()), nullptr, 0);
        if (!count) {
            Wh_Log(L"HTTP response is not valid UTF-8");
            return false;
        }
        response.body.resize(count);
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 responseBytes.data(),
                                 static_cast<int>(responseBytes.size()),
                                 response.body.data(), count)) {
            return false;
        }
    }
    return !IsDashboardRefreshCancelled() && GetTickCount64() < deadline;
}

}  // namespace http_detail

ULONGLONG CreateRefreshDeadline(const Settings& settings) {
    return GetTickCount64() +
           static_cast<ULONGLONG>(settings.requestTimeoutSeconds) * 1000;
}

// Callers share one deadline and publish only after a final cancellation check.
// Only reads are exposed; no login, quota refresh, reset or mutation operation.
bool Sub2ApiGet(const Settings& settings,
                const std::wstring& resource,
                ULONGLONG refreshDeadline,
                wdj::JsonObject& data,
                DWORD* responseStatus = nullptr) {
    data = nullptr;
    if (responseStatus) { *responseStatus = 0; }
    if (GetConnectionState(settings) != ConnectionState::Ready ||
        IsDashboardRefreshCancelled()) {
        return false;
    }
    // Resource is relative to /api/v1/admin; it can never replace the origin.
    if (resource.empty() || resource.front() != L'/' ||
        resource.find_first_of(L"\\\r\n#") != std::wstring::npos ||
        resource.find(L":") != std::wstring::npos ||
        resource.find(L"..") != std::wstring::npos ||
        resource.find(L"//") != std::wstring::npos) {
        Wh_Log(L"Sub2API request rejected: invalid resource");
        return false;
    }
    for (wchar_t ch : resource) {
        if (ch <= L' ' || ch == 127) {
            Wh_Log(L"Sub2API request rejected: invalid resource");
            return false;
        }
    }
    HttpResponse response;
    if (!http_detail::HttpGet(MakeSub2ApiRequest(settings, resource),
                              settings.requestTimeoutSeconds, refreshDeadline,
                              response)) {
        if (responseStatus) { *responseStatus = response.status; }
        if (!IsDashboardRefreshCancelled()) {
            Wh_Log(L"Sub2API transport failed: status=%u", response.status);
        }
        return false;
    }
    if (responseStatus) { *responseStatus = response.status; }
    if (response.status < 200 || response.status >= 300) {
        Wh_Log(L"Sub2API request failed: status=%u", response.status);
        return false;
    }
    wdj::JsonObject envelope = nullptr;
    wdj::JsonObject parsedData = nullptr;
    double code = -1;
    std::wstring message;
    if (!wdj::JsonObject::TryParse(response.body, envelope) ||
        !TryGetJsonNumber(envelope, L"code", code) ||
        !std::isfinite(code) || std::floor(code) != code ||
        !TryGetJsonString(envelope, L"message", message)) {
        Wh_Log(L"Sub2API response rejected: invalid envelope");
        return false;
    }
    if (code != 0) {
        // Never emit the server's message/reason: it may echo credentials.
        Wh_Log(L"Sub2API response rejected: nonzero code");
        return false;
    }
    if (!TryGetJsonObject(envelope, L"data", parsedData)) {
        Wh_Log(L"Sub2API response rejected: invalid data");
        return false;
    }
    if (IsDashboardRefreshCancelled() || GetTickCount64() >= refreshDeadline) {
        return false;
    }
    data = std::move(parsedData);
    return true;
}

// Only a GitHub token is accepted here; the public HTTPS origin cannot be changed.
// The version worker owns parsing and the shared hourly cache policy.
bool GitHubGetReleases(const std::wstring& githubToken,
                       DWORD timeoutSeconds,
                       ULONGLONG refreshDeadline,
                       HttpResponse& response) {
    response = {};
    HttpGetRequest request;
    if (!MakeGitHubReleaseRequest(githubToken, request)) {
        Wh_Log(L"GitHub request rejected: invalid token setting");
        return false;
    }
    return http_detail::HttpGet(request, timeoutSeconds, refreshDeadline, response);
}

}  // namespace sub2api_dashboard

