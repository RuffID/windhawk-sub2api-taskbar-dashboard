#pragma once

#include "platform.h"

namespace sub2api_dashboard {

// UI submits work; integration/runtime.h owns scheduling and refresh execution.
void QueueDashboardRefresh();
void RefreshDashboardText();
void StopDashboardRefreshWork();



}  // namespace sub2api_dashboard

