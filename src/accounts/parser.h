#pragma once

#include "../core/account_data.h"
#include "../core/text_json.h"
#include "../core/time.h"

namespace sub2api_dashboard {

FieldState ReadField(const wdj::JsonObject& object, PCWSTR name,
                     wdj::JsonValueType type, wdj::IJsonValue& value) {
    if (!object || !object.HasKey(name)) {
        return FieldState::Missing;
    }
    value = object.Lookup(name);
    if (value.ValueType() == wdj::JsonValueType::Null) {
        return FieldState::Null;
    }
    if (value.ValueType() != type) {
        return FieldState::Invalid;
    }
    return FieldState::Value;
}

DataField<double> ReadNumber(const wdj::JsonObject& object, PCWSTR name) {
    DataField<double> result;
    wdj::IJsonValue value = nullptr;
    result.state = ReadField(object, name, wdj::JsonValueType::Number, value);
    if (result.Known()) {
        result.value = value.GetNumber();
        if (!std::isfinite(result.value)) {
            result.state = FieldState::Invalid;
        }
    }
    return result;
}

DataField<double> ReadInteger(const wdj::JsonObject& object, PCWSTR name,
                              double minimum = 0, double maximum = 9007199254740991.0) {
    auto result = ReadNumber(object, name);
    if (result.Known() && (result.value < minimum || result.value > maximum ||
                           std::floor(result.value) != result.value)) {
        result.state = FieldState::Invalid;
    }
    return result;
}

DataField<std::wstring> ReadText(const wdj::JsonObject& object, PCWSTR name) {
    DataField<std::wstring> result;
    wdj::IJsonValue value = nullptr;
    result.state = ReadField(object, name, wdj::JsonValueType::String, value);
    if (result.Known()) {
        result.value = value.GetString().c_str();
    }
    return result;
}

DataField<bool> ReadBoolean(const wdj::JsonObject& object, PCWSTR name) {
    DataField<bool> result;
    wdj::IJsonValue value = nullptr;
    result.state = ReadField(object, name, wdj::JsonValueType::Boolean, value);
    if (result.Known()) {
        result.value = value.GetBoolean();
    }
    return result;
}

double UnixNowSeconds() {
    FILETIME time{};
    GetSystemTimeAsFileTime(&time);
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    return static_cast<double>(ticks.QuadPart - 116444736000000000ULL) / 10000000.0;
}

bool IsoUnixSeconds(const std::wstring& text, double& seconds) {
    FILETIME time{};
    if (!ParseIsoUtcFileTime(text, time)) {
        return false;
    }
    ULARGE_INTEGER ticks{};
    ticks.LowPart = time.dwLowDateTime;
    ticks.HighPart = time.dwHighDateTime;
    if (ticks.QuadPart < 116444736000000000ULL) {
        return false;
    }
    seconds = static_cast<double>(ticks.QuadPart - 116444736000000000ULL) / 10000000.0;
    return true;
}

QuotaWindow ParseQuotaWindow(const wdj::JsonObject& rateLimit, PCWSTR name) {
    QuotaWindow result;
    wdj::IJsonValue value = nullptr;
    result.state = ReadField(rateLimit, name, wdj::JsonValueType::Object, value);
    if (result.state != FieldState::Value) {
        return result;
    }
    auto window = value.GetObject();
    result.durationSeconds = ReadInteger(window, L"limit_window_seconds", 1);
    result.remainingPercent = ReadNumber(window, L"used_percent");
    if (result.remainingPercent.Known()) {
        if (result.remainingPercent.value < 0) {
            result.remainingPercent.state = FieldState::Invalid;
        } else {
            result.remainingPercent.value = std::max(0.0, 100.0 - result.remainingPercent.value);
        }
    }
    result.resetAt = ReadInteger(window, L"reset_at", 1, 253402300799.0);
    // reset_after_seconds without an absolute timestamp is not a new clock.
    return result;
}

AccountQuota ParseAccountQuota(const wdj::JsonObject& data, double now) {
    AccountQuota result;
    result.upstreamAccountId = ReadText(data, L"account_id");
    result.upstreamUserId = ReadText(data, L"user_id");
    for (auto* identity : {&result.upstreamAccountId, &result.upstreamUserId}) {
        if (identity->Known() && TrimString(identity->value).empty()) {
            identity->state = FieldState::Invalid;
        }
    }
    result.email = ReadText(data, L"email");
    result.fetchedAt = ReadInteger(data, L"fetched_at", 1, 253402300799.0);
    wdj::IJsonValue value = nullptr;
    FieldState rateState = ReadField(data, L"rate_limit", wdj::JsonValueType::Object, value);
    result.fiveHour.state = rateState;
    result.weekly.state = rateState;
    result.monthly.state = rateState;
    result.primarySlot.state = rateState;
    result.secondarySlot.state = rateState;
    if (rateState == FieldState::Value) {
        auto rate = value.GetObject();
        result.fiveHour.state = FieldState::Missing;
        result.weekly.state = FieldState::Missing;
        result.monthly.state = FieldState::Missing;
        result.primarySlot = ParseQuotaWindow(rate, L"primary_window");
        result.secondarySlot = ParseQuotaWindow(rate, L"secondary_window");
        for (const auto& window : {result.primarySlot, result.secondarySlot}) {
            if (!window.durationSeconds.Known()) {
                continue;
            }
            QuotaWindow* target = nullptr;
            if (window.durationSeconds.value == 18000) {
                target = &result.fiveHour;
            } else if (window.durationSeconds.value == 604800) {
                target = &result.weekly;
            } else if (window.durationSeconds.value >= 28 * 86400 &&
                       window.durationSeconds.value <= 31 * 86400) {
                target = &result.monthly;
            }
            if (target) {
                if (target->state == FieldState::Value || target->state == FieldState::Invalid) {
                    // Conflicting slots are ambiguous, never choose by array order.
                    target->state = FieldState::Invalid;
                    target->remainingPercent.state = FieldState::Invalid;
                } else {
                    *target = window;
                }
            }
        }
    }
    wdj::IJsonValue credits = nullptr;
    result.availableResetCredits.state = ReadField(
        data, L"rate_limit_reset_credits", wdj::JsonValueType::Object, credits);
    if (result.availableResetCredits.Known()) {
        auto object = credits.GetObject();
        auto count = ReadInteger(object, L"available_count", 0, INT_MAX);
        result.availableResetCredits.state = count.state;
        if (count.Known()) {
            result.availableResetCredits.value = static_cast<int>(count.value);
        }
        wdj::IJsonValue details = nullptr;
        if (ReadField(object, L"credits", wdj::JsonValueType::Array, details) == FieldState::Value) {
            double nearest = 0;
            for (const auto& detail : details.GetArray()) {
                if (detail.ValueType() != wdj::JsonValueType::Object) {
                    continue;
                }
                auto expiry = ReadText(detail.GetObject(), L"expires_at");
                double timestamp = 0;
                if (expiry.Known() && IsoUnixSeconds(expiry.value, timestamp) &&
                    timestamp > now && (nearest == 0 || timestamp < nearest)) {
                    nearest = timestamp;
                    result.nearestResetExpiry = expiry.value;
                }
            }
        }
    }
    // additional_rate_limits (Spark), credits and credentials are deliberately unread.
    return result;
}

struct AccountPage {
    std::vector<AccountSnapshot> accounts;
    size_t total = 0;
    size_t page = 0;
    size_t pageSize = 0;
    size_t pages = 0;
    bool itemsValid = true;
};

bool ParseAccountPage(const wdj::JsonObject& data, size_t requestedPage,
                      AccountPage& result) {
    auto total = ReadInteger(data, L"total", 0, INT_MAX);
    auto page = ReadInteger(data, L"page", 1, INT_MAX);
    auto pageSize = ReadInteger(data, L"page_size", 1, 100);
    auto pages = ReadInteger(data, L"pages", 1, INT_MAX);
    wdj::IJsonValue items = nullptr;
    if (!total.Known() || !page.Known() || !pageSize.Known() ||
        page.value != requestedPage ||
        ReadField(data, L"items", wdj::JsonValueType::Array, items) != FieldState::Value) {
        return false;
    }
    const double pageCount = std::max(1.0, std::ceil(total.value / pageSize.value));
    // Some deployed servers omit pages. Only absence permits deriving it;
    // a supplied invalid or contradictory value still rejects the response.
    if (pages.state != FieldState::Missing && (!pages.Known() || pages.value != pageCount)) {
        return false;
    }
    result = {};
    result.total = static_cast<size_t>(total.value);
    result.page = static_cast<size_t>(page.value);
    result.pageSize = static_cast<size_t>(pageSize.value);
    result.pages = static_cast<size_t>(pageCount);
    if (items.GetArray().Size() > result.pageSize) {
        return false;
    }
    for (const auto& item : items.GetArray()) {
        if (item.ValueType() != wdj::JsonValueType::Object) {
            result.itemsValid = false;
            continue;
        }
        auto object = item.GetObject();
        auto id = ReadInteger(object, L"id", 1);
        auto platform = ReadText(object, L"platform");
        if (!id.Known() || !platform.Known() || platform.value != L"openai") {
            result.itemsValid = false;
            continue;
        }
        AccountSnapshot account;
        account.localId = static_cast<int64_t>(id.value);
        account.name = ReadText(object, L"name");
        account.type = ReadText(object, L"type");
        account.status = ReadText(object, L"status");
        account.schedulable = ReadBoolean(object, L"schedulable");
        wdj::JsonObject projection = nullptr;
        if (TryGetJsonObject(object, L"credentials", projection)) {
            account.email = ReadText(projection, L"email");
        }
        if (!account.email.Known() && TryGetJsonObject(object, L"extra", projection)) {
            account.email = ReadText(projection, L"email");
        }
        if (account.type.Known() && account.type.value == L"oauth") {
            account.quotaRead = QuotaReadState::Pending;
        }
        result.accounts.push_back(std::move(account));
    }
    return true;
}

}  // namespace sub2api_dashboard
