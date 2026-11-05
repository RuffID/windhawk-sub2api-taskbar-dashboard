# Итоговая проверка 05.11.2026

[Техническая документация](README.md)

## Результат

План завершён в явно согласованном объёме. Реализация не менялась.
Пользователь сообщил об успешной компиляции мода в Windhawk, включении,
корректном отображении, устраивающих оформлении и функциональности
при одном аккаунте Codex. Это свидетельство пользователя, а не запуск агента.

| Проверка | Результат и источник |
| --- | --- |
| Сборка мода в Windhawk | Подтверждена пользователем |
| Обычная работа и внешний вид, один аккаунт Codex | Приняты пользователем |
| Сборка tests/account_data.cpp | Exit 0; один warning signed/unsigned в строке 483 |
| Запуск account_data.exe | Exit 0; Account data checks passed |
| Сборка tests/popup_versions.cpp | Exit 0; диагностик нет |
| Запуск popup_versions.exe | Exit 0; Popup and version checks passed |

Оба main вызывают по семь групп проверок. Это два самостоятельных executable,
а не отчёт о числе отдельных assertions. Проверяются рабочие parser/loader,
weekly/publication, version/cache и UI helpers, а не копии production-формулы.

## Команды

Рабочий каталог компиляции: `D:/Programs/Windhawk/Compiler`.
Флаги взяты из `editor/compile_commands.json` нового проекта:
первый элемент arguments — компилятор, последний — исходник мода.
Для tests последний элемент заменён соответствующим C++ файлом, добавлен `-o`.
Оба файла используют WH_EDITING no-host stubs.

Выполненная подготовка и компиляция (PowerShell):

```powershell
$ErrorActionPreference = 'Stop'
$taskRoot = 'D:\Media\User\source\repos\windhawk-sub2api-taskbar-dashboard'
$taskOutput = Join-Path $taskRoot 'artifacts\validation-2026-11-05'
New-Item -ItemType Directory -Force -Path $taskOutput | Out-Null
$taskDatabase = Get-Content -Raw -Encoding UTF8 (Join-Path $taskRoot 'editor\compile_commands.json') | ConvertFrom-Json
$taskFlags = @($taskDatabase[0].arguments | Select-Object -Skip 1)
$taskFlags = @($taskFlags | Select-Object -SkipLast 1)
& 'D:\Programs\Windhawk\Compiler\bin\clang++.exe' @taskFlags (Join-Path $taskRoot 'tests\account_data.cpp') '-o' (Join-Path $taskOutput 'account_data.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'account_data-build.log')
# Exit 0; один warning -Wsign-compare.
& 'D:\Programs\Windhawk\Compiler\bin\clang++.exe' @taskFlags (Join-Path $taskRoot 'tests\popup_versions.cpp') '-o' (Join-Path $taskOutput 'popup_versions.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'popup_versions-build.log')
# Exit 0; диагностик нет.
```

Фактические общие аргументы компиляции:

```text
-x c++ -std=c++23 -target x86_64-w64-mingw32
-DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00
-D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008
-D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING
-include windhawk_api.h -Wall -Wextra -Wno-unused-parameter
-Wno-missing-field-initializers -Wno-cast-function-type-mismatch
-lole32 -loleaut32 -lruntimeobject -lgdi32 -lwinhttp -lshell32
-ID:/Media/User/source/repos/windhawk-sub2api-taskbar-dashboard
```

Каждый executable запускался отдельно с проверкой кода выхода:

```powershell
$taskOutput = 'D:\Media\User\source\repos\windhawk-sub2api-taskbar-dashboard\artifacts\validation-2026-11-05'
$env:PATH = 'D:\Programs\Windhawk\AppData\Engine\Mods\64;D:\Programs\Windhawk\Compiler\x86_64-w64-mingw32\bin;' + $env:PATH
& (Join-Path $taskOutput 'account_data.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'account_data-run.log')
# Exit 0: Account data checks passed
& (Join-Path $taskOutput 'popup_versions.exe') 2>&1 | Tee-Object -FilePath (Join-Path $taskOutput 'popup_versions-run.log')
# Exit 0: Popup and version checks passed
```

В первом запуске account_data.exe в PATH был только runtime Compiler:
Windows вернул 0xC0000135 до main. Чтением PE imports установлена зависимость
от `libc++.whl` и `libunwind.whl`; их x64-файлы найдены в Engine/Mods/64.
Добавление этого каталога в PATH процесса устранило ошибку загрузки.
Глобальный PATH, установка Windhawk и исходники не изменялись.
Логи выполненных tests и бинарные результаты находятся в исключённом
`artifacts/validation-2026-11-05/`; они не являются поставкой мода.

## Покрытие и изоляция

`account_data.cpp`: страницы с pages и без него, пустой/изменяющийся список,
повтор ID, full/scoped DTO, нормализация окон/Reset, неизвестные поля,
среднее/дубли/округление, 899/900/901 секунд, устаревший timestamp, частичные
ошибки и исключение request, bounded workers с join, отмена и generation gate.

`popup_versions.cpp`: SemVer и prerelease, server/GitHub UI-состояния,
лимиты и Retry-After, builders авторизации и отклонение недопустимых токенов,
сохранение паузы при смене токена, временный persistent cache и два клиента,
повреждённый кеш/отмена/исключение, Reset/геометрия/сортировка и publication gate.

Ответы, часы и stop подставлены в памяти. Нет HTTP/listener, Windhawk/Explorer,
окон, БД, пользовательского кеша или реальных реквизитов.
Тест кеша использует уникальные временные файлы/mutex с RAII-очисткой.
Link с WinHTTP/GDI не означает реального сетевого запроса или открытия UI.

## Согласованные исключения и непроверенные сценарии

Из ручной приёмки исключены несколько аккаунтов/прокрутка, другие DPI/мониторы
и совместное включение с codex-lb-модом. Поддержка остаётся в исходниках;
расчёт нескольких аккаунтов и часть геометрии проверены синтетически.

Пользователь явно разрешил закрыть план с непроверенными в Explorer сценариями:
ручное обновление, смена настроек, выключение/повторное включение, восстановление
после недоступности сервера и восстановление taskbar/XAML. Эти сценарии не
помечаются пройденными. Изолированные проверки ошибок/отмены не заменяют их.

Отдельно не проверялись реальный WinHTTP при задержках/разрывах, журналы живого
мода, права ограниченного ключа на развёрнутом сервере, серверные побочные эффекты
полного GET, разные Windows-сессии для mutex/кеша и чистая установка на другой машине.
Сравнительные скриншоты не предоставлялись; точная версия сервера и текущие
параметры мониторов заново не снимались. Ранее подтверждён HTTP 200 quota полным ключом;
локальный исходник sub2api не доказывает поддержку scoped quota развёрнутой версией.

Инструкция установки/обновления описана в [README](../../README.md).
Агент не выполнял Git-операции, публикацию, реальные сетевые проверки,
изменение настроек, запуск Windhawk или перезапуск Explorer.

## Проверка документации

Проверены 42 Markdown-файла: строгий UTF-8 без повреждений текста, 155 локальных
ссылок с существующими целями, 17 документов со статусом completed и отсутствие
незакрытых чекбоксов в завершённом плане. Имя папки плана и входящие ссылки
синхронизированы. Исторические результаты завершённых этапов 01/02 сохранены.
