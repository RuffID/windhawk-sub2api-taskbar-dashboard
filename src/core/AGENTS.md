# Общая основа

platform.h содержит Windhawk/Win32/WinRT и стандартные зависимости.
state.h — единственный владелец globals, Settings, UI-моделей и ресурсов.
account_data.h — типизированные поля с Missing/Null/Invalid/Value, окна и snapshot
пула. Только accounts/parser.h создаёт их из JSON. Upstream-пара — только для
дедупликации в памяти; в UI-модель и логи она не попадает.
Пул и его UI-проекции публикуются под g_dashboardMutex; generation под тем же
mutex блокирует результаты старых настроек. Не сохранять ключ в snapshot пула.
Не переносить секрет в DashboardAccount/DashboardVersion или текст UI.
Неизвестные проценты — -1; не создавать тестовые аккаунты в production.
text_json.h — кодировки, JSON и форматирование; time.h — сроки;
refresh_api.h разрывает зависимость popup от integration/runtime.h.

logging.h пишет только безопасное имя операции и HRESULT в отдельные файлы
нового мода. Не логировать URL с реквизитами, ключи, заголовки и ответы сервера.
Независимые ID/COM CLSID/имена ресурсов не заменять исходными.

