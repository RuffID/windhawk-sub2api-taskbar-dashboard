// ==WindhawkMod==
// @id              sub2api-taskbar-dashboard
// @name            Sub2API Taskbar Dashboard
// @description     OpenAI weekly remainder, quota popup and sub2api release status
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
Cards retain the original two-column layout, quota reset times and Reset credits,
with a separate data-quality line and no elapsed age text. Monthly is never a weekly mean.
Server version/check-updates and independent public GitHub releases (including
prereleases) run separately from account refresh. Errors do not imply up-to-date.

Set the sub2api base URL (optionally with a reverse-proxy path prefix) and an
administrator API key in Windhawk settings. Replace the examples in both fields;
examples and empty values mean unconfigured. No login, cookies or environment
credentials are used. The admin key is sent only to sub2api in x-api-key.
An optional GitHub token raises the public API allowance; it is sent only to
api.github.com as Bearer, never to sub2api, logs or the release cache. Leaving the
token empty or keeping its example uses anonymous requests. Changing the token
does not bypass the hourly minimum or a persisted GitHub rate-limit pause.

Full and restricted administrator keys are supported without guessing permissions
from the key prefix. Restricted access needs accounts.read, accounts.identity.read,
openai.quota.read for the intended OpenAI accounts and global system.read.
Quota reads with a full key can trigger the server's already enabled
auto-reset check and consume Reset credits. This is allowed. The mod does not
configure auto-reset or call separate reset operations.

Settings changes and unloading cancel outstanding requests and drain refresh work.
Each request has a timeout, shares the caller's total deadline, and accepts at
most 4 MiB. Redirects, cookies and automatic authentication are disabled.

The server releases link points to Wei-Shaw/sub2api. The icon beside the mod
version opens RuffID/windhawk-sub2api-taskbar-dashboard. The release cache and interprocess mutex
enforce an hourly minimum and GitHub Retry-After/rate limits across manual refresh
and reload. Taskbar update status comes only from the server's has_update.
Logs and release cache use Sub2ApiTaskbarDashboard, separate from the
original mod. The existing mod is not modified.
Implementation has only been inspected statically; compilation, isolated fixtures
and Windhawk/visual acceptance have not been performed.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- sub2apiUrl: "https://sub2api.example.com"
  $name: Адрес sub2api
  $description: Замените пример своим адресом, например http://192.168.1.10:8080. Допускается префикс reverse proxy. Без /api/v1/admin, query, fragment, логина и пароля. С примером запросы не отправляются.
- adminApiKey: "YOUR_SUB2API_ADMIN_KEY"
  $name: Администраторский API-ключ
  $description: Замените пример целым ключом sub2api, без Bearer и кавычек. В sub2api 0.2.19 для квот нужен прежний полный ключ; все галочки у adm_sc_ не дают доступа к квоте. Полный ключ допускает проверку уже включённого автосброса сервером. С примером запросы не отправляются.
- githubToken: "YOUR_GITHUB_TOKEN_OR_LEAVE_EMPTY"
  $name: GitHub-токен (необязательно)
  $description: Замените пример своим GitHub PAT без Bearer и кавычек или оставьте поле пустым. Для публичных релизов достаточно чтения публичных репозиториев. Токен отправляется только api.github.com. Пустое поле или пример — без токена. Часовая пауза и Retry-After сохраняются.
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

