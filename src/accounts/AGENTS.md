# Аккаунты

parser.h — единственная граница JSON: проекция списка и прямой quota в модели
core/account_data.h. Не сохранять JSON, credentials/extra целиком или денежные
квоты. Полная upstream-пара хранится только в памяти для дедупликации.

pool.h владеет пагинацией и bounded quota workers; каждый worker завершается
и join выполняется до возврата, в том числе при исключении. request/stop передаются
как зависимости для изолированных fixtures. Один deadline создаёт runtime.
weekly.h — пригодность, давность, дедупликация и единственное сводное округление.
presentation.h строит только UI-проекцию согласованного snapshot, без HTTP.
publication.h проверяет cancel/generation и публикует согласованные поля под
g_dashboardMutex. Его безопасная диагностика не получает JSON/Settings.

Ошибки не обновляют fetched_at, пропавшая идентичность успешного ответа не
восстанавливается из кеша. Неполная выборка не доказывает удаление записи.
Логи: короткие English events, local ID/page/status/counts; без name/email,
upstream IDs, ключей и тел ответов. Никаких POST/reset/настройки автосброса.
