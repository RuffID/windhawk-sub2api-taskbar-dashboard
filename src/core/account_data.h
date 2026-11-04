#pragma once

#include "platform.h"

namespace sub2api_dashboard {

// External values keep their provenance: missing/null/invalid never mean zero.
enum class FieldState { Missing, Null, Invalid, Value };

template <typename T>
struct DataField {
    FieldState state = FieldState::Missing;
    T value{};
    bool Known() const { return state == FieldState::Value; }
};

struct QuotaWindow {
    FieldState state = FieldState::Missing;
    DataField<double> durationSeconds;
    DataField<double> remainingPercent;
    DataField<double> resetAt;
};

struct AccountQuota {
    DataField<std::wstring> upstreamAccountId;
    DataField<std::wstring> upstreamUserId;
    DataField<std::wstring> email;
    DataField<double> fetchedAt;
    QuotaWindow primarySlot;
    QuotaWindow secondarySlot;
    QuotaWindow fiveHour;
    QuotaWindow weekly;
    QuotaWindow monthly;
    DataField<int> availableResetCredits;
    std::wstring nearestResetExpiry;
};

enum class QuotaReadState { NotSupported, Pending, Succeeded, Failed };

struct AccountSnapshot {
    int64_t localId = 0;
    DataField<std::wstring> name;
    DataField<std::wstring> email;
    DataField<std::wstring> type;
    DataField<std::wstring> status;
    DataField<bool> schedulable;
    bool discovered = true;
    QuotaReadState quotaRead = QuotaReadState::NotSupported;
    AccountQuota quota;
};

struct WeeklySummary {
    int percent = -1;
    size_t usable = 0;
    size_t expected = 0;
    bool incomplete = true;
    bool expectedIsExact = false;
    double oldestFetchedAt = 0;
};

struct AccountPoolSnapshot {
    std::vector<AccountSnapshot> accounts;
    size_t expectedRecords = 0;
    bool discoveryComplete = false;
    bool quotaReadsSucceeded = true;
    bool interrupted = false;
    WeeklySummary summary;
    bool fullySuccessful = false;
};

}  // namespace sub2api_dashboard
