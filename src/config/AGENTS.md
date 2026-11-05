# Настройки

settings.h читает Windhawk sub2apiUrl/adminApiKey, необязательный githubToken и
интервалы. Строковые defaults — примеры внутри полей; WithoutSettingExample
превращает точные примеры в пустые значения. Примеры не отправляются в сеть.
Переменные окружения с credentials не используются.
Wh_GetStringSetting освобождается RAII; g_settings заменяется целым snapshot.
Изменение настроек координирует integration/lifecycle.h после отмены и drain.

URL — базовый HTTP(S) с необязательным префиксом reverse proxy, без
/api/v1/admin, userinfo/query/fragment/controls. Ключ — безопасное значение
заголовка; его префикс не доказывает права. Ошибка настроек блокирует запрос,
не подставлять резервный сервер или credentials.
githubToken проверяется отдельно на HTTP-границе; его ошибка не блокирует
аккаунты или версии sub2api. Пустой токен означает анонимный запрос GitHub.
Не определять тип или права ключей по префиксу. Примеры в mod.wh.cpp и константы
settings.h должны совпадать. Смена токена не очищает persistent cache/паузу.

