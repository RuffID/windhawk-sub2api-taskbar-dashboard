// ==WindhawkMod==
// @id              sub2api-taskbar-dashboard
// @name            Sub2API Taskbar Dashboard
// @description     OpenAI account weekly remainder from sub2api, with partial-data and age indicators
// @version         0.1
// @author          P.D.G.
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lole32 -loleaut32 -lruntimeobject -lgdi32 -lwinhttp -lshell32 -I"D:/Media/User/source/repos/windhawk-sub2api-taskbar-dashboard"
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Sub2API Taskbar Dashboard

Independent foundation adapted from Codex LB Taskbar Dashboard 1.3.
Preserves the taskbar widget, popup styling, DPI scaling and lifecycle.
Loads every accessible OpenAI account page and queries OAuth quota with bounded
concurrency. Shows the weekly mean with partial-data and age indicators.
Version queries and the full card adaptation belong to stage 04.

Set the sub2api base URL (optionally with a reverse-proxy path prefix) and an
administrator API key in Windhawk settings. Both defaults are empty. No requests
are sent without a valid connection. No login, cookies or environment credentials
are used. The key is sent only to sub2api in x-api-key, never to GitHub or logs.

Full and restricted administrator keys are supported without guessing permissions
from the key prefix. Restricted access needs accounts.read, accounts.identity.read,
openai.quota.read for the intended OpenAI accounts and global system.read.
Quota reads with a full key can trigger the server's already enabled
auto-reset check and consume Reset credits. This is allowed. The mod does not
configure auto-reset or call separate reset operations.

Settings changes and unloading cancel outstanding requests and drain refresh work.
Each request has a timeout, shares the caller's total deadline, and accepts at
most 4 MiB. Redirects, cookies and automatic authentication are disabled.

The server releases link points to Wei-Shaw/sub2api. The new mod repository URL
is not known, so its link is disabled. Version querying belongs to stage 04.
Logs and future release cache use Sub2ApiTaskbarDashboard, separate from the
original mod. The existing mod is not modified.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- sub2apiUrl: ""
  $name: Адрес sub2api
  $description: Базовый HTTP(S) адрес, при необходимости с префиксом reverse proxy. Без /api/v1/admin, query, fragment, логина и пароля. По умолчанию пустой.
- adminApiKey: ""
  $name: Администраторский API-ключ
  $description: Полный или ограниченный ключ. Для ограниченного нужны accounts.read, accounts.identity.read, openai.quota.read и system.read. Полный ключ допускает серверную проверку уже включённого автосброса после чтения квоты. По умолчанию пустой.
- updateIntervalSeconds: 60
  $name: Интервал обновления, секунды
  $description: Интервал чтения аккаунтов и квот от 30 до 2147483 секунд. Возраст данных проверяется отдельно.
- requestTimeoutSeconds: 30
  $name: Таймаут запроса, секунды
  $description: От 30 до 2147483 секунд; каждый запрос также ограничен общим deadline обновления.
*/
// ==/WindhawkModSettings==

#include "src/integration/lifecycle.h"

BOOL Wh_ModInit() {
    return sub2api_dashboard::InitializeMod();
}

void Wh_ModSettingsChanged() {
    sub2api_dashboard::ApplySettingsChanged();
}

void Wh_ModAfterInit() {
    sub2api_dashboard::AfterInitializeMod();
}

void Wh_ModUninit() {
    sub2api_dashboard::UninitializeMod();
}

