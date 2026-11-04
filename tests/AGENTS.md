# Изолированные проверки

account_data.cpp — отдельная единица трансляции с синтетическими JSON fixtures.
Вызывает production parser, pool loader, weekly calculation и publication gate.
Ответы request/clock/stop подставлены в памяти. Нет HTTP/listener, Windhawk,
Explorer, UI, БД, GitHub, пользовательских файлов или реальных реквизитов.
Допустимы WinRT JSON, Win32 event и bounded std::thread, освобождаемые владельцем.
Сообщения assertions не содержат email, upstream-пары и JSON.

Не заменять production-формулу её копией. Проверять внешние результаты и
ошибки/отмену наряду с успешным сценарием. Компиляция и запуск требуют отдельного
разрешения на точную команду; наличие fixtures не подтверждает их прохождение.
