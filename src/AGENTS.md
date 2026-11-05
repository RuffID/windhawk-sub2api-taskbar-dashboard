# Реализация

Все заголовки входят через integration/lifecycle.h в корневой mod.wh.cpp:
одна единица трансляции, namespace sub2api_dashboard, глобально только COM exports.
Внутренние includes относительные; #pragma once, явные зависимости без циклов.
Порядок: core → config/network/accounts/versions → UI → integration.
Не добавлять production .cpp или отдельную систему сборки.

core владеет общими моделями; config — snapshot настроек; network — WinHTTP;
versions — SemVer, контракты версий, UI-состояния и persistent cache; ui — taskbar/popup; integration — runtime
и lifecycle. accounts — parser, bounded loader, weekly calculation и UI-проекция.
Runtime независимо планирует чтение аккаунтов/quota и версий. Версии не меняют
сводку аккаунтов или время её успеха и не продлевают account deadline.
Нет вымышленных процентов и отдельной диагностики переавторизации.
При создании/изменении границ папок актуализировать ближайшие AGENTS.md.
Компиляция и любые исполняемые проверки требуют отдельного разрешения.

