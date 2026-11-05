# Изолированные проверки

account_data.cpp — отдельная единица трансляции с синтетическими JSON fixtures.
Вызывает production parser, pool loader, weekly calculation и publication gate.
Ответы request/clock/stop подставлены в памяти. Нет HTTP/listener, Windhawk,
Explorer, UI, БД, GitHub, пользовательских файлов или реальных реквизитов.
Допустимы WinRT JSON, Win32 event и bounded std::thread, освобождаемые владельцем.
Сообщения assertions не содержат email, upstream-пары и JSON.

popup_versions.cpp — отдельная единица трансляции для production SemVer,
проекции server version/check-updates, выбора GitHub-релиза, UI-состояний,
URL encoding, Reset/геометрии колонок, сортировки и publication gate.
CheckPublishedRelease получает фиктивный request/stop/clock. Тесты persistent
cache используют только уникальный временный файл и mutex, два клиента/потока,
удаляют свои файлы через RAII; пользовательский кеш/логи не открываются.
Нет HTTP, окон или запуска мода. Это не визуальная приёмка; fixtures не запускались.

Не заменять production-формулу её копией. Проверять внешние результаты и
ошибки/отмену наряду с успешным сценарием. Компиляция и запуск требуют отдельного
разрешения на точную команду; наличие fixtures не подтверждает их прохождение.
