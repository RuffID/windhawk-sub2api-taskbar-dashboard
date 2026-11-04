# Настройки

settings.h читает Windhawk sub2apiUrl/adminApiKey и интервалы. Адрес/ключ пустые
по умолчанию, переменные окружения с credentials не используются.
Wh_GetStringSetting освобождается RAII; g_settings заменяется целым snapshot.
Изменение настроек координирует integration/lifecycle.h после отмены и drain.

URL — базовый HTTP(S) с необязательным префиксом reverse proxy, без
/api/v1/admin, userinfo/query/fragment/controls. Ключ — безопасное значение
заголовка; его префикс не доказывает права. Ошибка настроек блокирует запрос,
не подставлять резервный сервер или credentials.

