# Версии

semver.h — сравнение stable/prerelease (beta.10 > beta.9, stable > beta).
releases.h — проекция data.version/has_update/latest_version/release_info.html_url и публичных releases,
rate-limit policy и транзакция read/reserve/request/settle с подставными request,
stop/clock для fixtures. presentation.h — различимые UI-состояния и URL encoding
исходного tag как одного сегмента пути; знак v/build metadata не теряются.
release_cache.h — атомарный файловый кеш под межпроцессным mutex.
Отдельные каталог Sub2ApiTaskbarDashboard и Global mutex не пересекаются с codex-lb.
Кеш содержит только tag/status/deadline, никогда Settings или ключ.

Источник релизов — Wei-Shaw/sub2api, включая prerelease, без drafts. Запрос только
через GitHubGetReleases без Settings/административного ключа; только отдельный
необязательный GitHub-токен из snapshot владельца work. Токен не попадает в кеш,
его изменение не сбрасывает паузу и не создаёт другой кеш/mutex.
Taskbar использует серверный has_update;
warning не считается подтверждённой проверкой. GitHub проверяется независимо.
Popup показывает подтверждённую серверную latest и заменяет её GitHub-релизом
только при большем SemVer. Ошибка/пауза GitHub не скрывает серверное обновление;
неизвестный номер не превращает has_update в «Нет новых версий».
Неверный/пустой ответ не означает актуальность. Логи — короткое событие/status/HR,
серверные и GitHub message/body/headers не выводятся.

Mutex удерживается через весь запрос: ожидание до 15 секунд с cancel, HTTP до
10 секунд. Слот записывается до HTTP, Failed/RateLimited/Succeeded/Unknown — после.
Отмена и исключение завершают Pending с сохранением паузы. После crash Pending
ограничен сохранённым nextCheckUtc. Ошибка кеша запрещает несогласованный HTTP.
Manual/reload не обходят часовой минимум. 429, а также 403 с remaining=0,
Retry-After или rate-limit/abuse message — RateLimited; обычный 403 — Failed.
Более поздние Reset/Retry-After (секунды/HTTP-date) продлевают паузу, включая
успешный ответ с remaining=0. Без срока остаётся минимум один час.
Проверки production-кода находятся в tests/popup_versions.cpp; результаты и
границы выполненных проверок — в Documentation/Technical documentation/popup-and-versions.md.

