#pragma once

#include "semver.h"
#include "../core/state.h"

namespace sub2api_dashboard {

std::wstring ReleaseTagUrl(const std::wstring& tag) {
    const auto bytes = WideToUtf8(tag);
    std::wstring encoded;
    constexpr wchar_t hex[] = L"0123456789ABCDEF";
    for (unsigned char ch : bytes) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded += static_cast<wchar_t>(ch);
        } else {
            encoded += L'%';
            encoded += hex[ch >> 4];
            encoded += hex[ch & 15];
        }
    }
    return std::wstring(SUB2API_GITHUB_URL) + L"/releases/tag/" + encoded;
}

constexpr COLORREF POPUP_DEFAULT_TEXT_COLOR = RGB(198, 209, 224);
constexpr COLORREF POPUP_DEFAULT_BORDER_COLOR = RGB(46, 62, 84);

struct PopupReleaseBadge {
    std::wstring text;
    std::wstring releaseUrl;
    COLORREF borderColor = POPUP_DEFAULT_BORDER_COLOR;
    COLORREF textColor = POPUP_DEFAULT_TEXT_COLOR;
};

PopupReleaseBadge GetPopupReleaseBadge(const DashboardVersion& version) {
    if (version.serverCheckStatus == ReleaseCheckStatus::Succeeded && version.latestUpdateAvailable) {
        ReleaseVersion serverLatest, githubLatest;
        const bool hasServerLatest = ParseReleaseVersion(version.serverLatest, serverLatest);
        if (hasServerLatest && version.releaseCheckStatus == ReleaseCheckStatus::Succeeded &&
            ParseReleaseVersion(version.latest, githubLatest) && CompareReleaseVersions(githubLatest, serverLatest) > 0) {
            return {L"Доступно: " + version.latest, ReleaseTagUrl(version.latest), RGB(106, 212, 124)};
        }
        std::wstring url = version.serverReleaseUrl;
        if (url.empty()) { url = std::wstring(SUB2API_GITHUB_URL) + L"/releases/latest"; }
        if (hasServerLatest) {
            return {L"Доступно: " + version.serverLatest, url, RGB(106, 212, 124)};
        }
        return {L"Доступна latest-версия", url, RGB(106, 212, 124)};
    }
    if (version.releaseCheckStatus == ReleaseCheckStatus::Pending) {
        return {L"Ожидание проверки GitHub", {}};
    }
    if (version.releaseCheckStatus == ReleaseCheckStatus::RateLimited) {
        return {L"Лимит запросов GitHub", {}, RGB(242, 113, 113)};
    }
    if (version.releaseCheckStatus == ReleaseCheckStatus::Unknown) {
        return {L"Версия релиза неизвестна", {}};
    }
    ReleaseVersion latest, current;
    if (version.releaseCheckStatus == ReleaseCheckStatus::Failed || !ParseReleaseVersion(version.latest, latest)) {
        return {L"Ошибка проверки GitHub", {}, RGB(242, 113, 113)};
    }
    if (version.currentStatus == ReleaseCheckStatus::Pending) {
        return {L"Ожидание версии сервера", {}};
    }
    if (version.currentStatus == ReleaseCheckStatus::Failed || version.currentStatus == ReleaseCheckStatus::RateLimited) {
        return {L"Не удалось прочитать версию сервера", {}, RGB(242, 113, 113)};
    }
    if (!ParseReleaseVersion(version.current, current)) {
        return {L"Текущая версия неизвестна", {}};
    }
    if (CompareReleaseVersions(latest, current) > 0) {
        return {L"Доступно: " + version.latest, ReleaseTagUrl(version.latest), RGB(106, 212, 124)};
    }
    return {L"Нет новых версий", {}};
}

std::wstring CurrentVersionText(const DashboardVersion& version) {
    std::wstring value;
    if (version.currentStatus == ReleaseCheckStatus::Pending) { value = L"ожидание версии"; }
    else if (version.currentStatus == ReleaseCheckStatus::Failed) { value = L"ошибка версии"; }
    else if (version.currentStatus == ReleaseCheckStatus::RateLimited) { value = L"лимит запросов"; }
    else if (version.current.empty()) { value = L"н/д"; }
    else { value = version.current; }
    // Put a failed server check first so clipping a long version cannot hide it.
    if (version.serverCheckStatus == ReleaseCheckStatus::Failed) { return L"Проверка: ошибка · " + value; }
    if (version.serverCheckStatus == ReleaseCheckStatus::RateLimited) { return L"Проверка: лимит · " + value; }
    if (version.serverCheckStatus == ReleaseCheckStatus::Pending) { return L"Проверка: ожидание · " + value; }
    return L"Текущая: " + value;
}

bool ShouldShowLatestUpdateIndicator(const DashboardVersion& version, int) {
    return version.serverCheckStatus == ReleaseCheckStatus::Succeeded && version.latestUpdateAvailable;
}

}  // namespace sub2api_dashboard
