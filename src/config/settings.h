#pragma once

#include "../core/state.h"
#include "../core/text_json.h"

namespace sub2api_dashboard {

DWORD ClampSeconds(int value) {
    if (value < static_cast<int>(MIN_SETTING_SECONDS)) {
        return MIN_SETTING_SECONDS;
    }
    if (value > static_cast<int>(MAX_SETTING_SECONDS)) {
        return MAX_SETTING_SECONDS;
    }
    return static_cast<DWORD>(value);
}

std::wstring ReadStringSetting(PCWSTR name) {
    PCWSTR value = Wh_GetStringSetting(name);
    struct SettingGuard {
        PCWSTR value;
        ~SettingGuard() {
            if (value) {
                Wh_FreeStringSetting(value);
            }
        }
    } guard{value};
    return value ? std::wstring(value) : std::wstring{};
}

void LoadSettings() {
    Settings settings;
    settings.sub2apiUrl = TrimString(ReadStringSetting(L"sub2apiUrl"));
    while (!settings.sub2apiUrl.empty() && settings.sub2apiUrl.back() == L'/') {
        settings.sub2apiUrl.pop_back();
    }
    settings.adminApiKey = ReadStringSetting(L"adminApiKey");
    settings.updateIntervalSeconds =
        ClampSeconds(Wh_GetIntSetting(L"updateIntervalSeconds"));
    settings.requestTimeoutSeconds =
        ClampSeconds(Wh_GetIntSetting(L"requestTimeoutSeconds"));

    std::lock_guard<std::mutex> lock(g_settingsMutex);
    g_settings = std::move(settings);
}

Settings GetSettingsSnapshot() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

enum class ConnectionState { NotConfigured, Invalid, Ready };

bool IsValidBaseUrl(const std::wstring& url) {
    if (url.empty() || url.size() > 8192 ||
        url.find_first_of(L"?#\\") != std::wstring::npos) {
        return false;
    }
    for (wchar_t ch : url) {
        if (ch <= L' ' || ch == 127) {
            return false;
        }
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUserNameLength = static_cast<DWORD>(-1);
    parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        return false;
    }
    return (parts.nScheme == INTERNET_SCHEME_HTTP ||
            parts.nScheme == INTERNET_SCHEME_HTTPS) &&
           parts.dwHostNameLength > 0 && parts.nPort != 0 &&
           parts.dwUserNameLength == 0 && parts.dwPasswordLength == 0;
}

ConnectionState GetConnectionState(const Settings& settings) {
    if (settings.sub2apiUrl.empty() || settings.adminApiKey.empty()) {
        return ConnectionState::NotConfigured;
    }
    if (!IsValidBaseUrl(settings.sub2apiUrl) || settings.adminApiKey.size() > 4096) {
        return ConnectionState::Invalid;
    }
    // Header values must not introduce whitespace, controls or another header.
    for (wchar_t ch : settings.adminApiKey) {
        if (ch <= L' ' || ch >= 127) {
            return ConnectionState::Invalid;
        }
    }
    return ConnectionState::Ready;
}

std::wstring GetFoundationText(const Settings& settings) {
    switch (GetConnectionState(settings)) {
        case ConnectionState::NotConfigured:
            return L"Подключение не настроено";
        case ConnectionState::Invalid:
            return L"Некорректные настройки";
        case ConnectionState::Ready:
            return L"Нет данных";
    }
    return L"Нет данных";
}

}  // namespace sub2api_dashboard

