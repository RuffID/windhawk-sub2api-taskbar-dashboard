# Реализация

Все заголовки входят через integration/lifecycle.h в корневой mod.wh.cpp:
одна единица трансляции, namespace sub2api_dashboard, глобально только COM exports.
Внутренние includes относительные; #pragma once, явные зависимости без циклов.
Порядок: core → config/network/versions → UI → integration.
Не добавлять production .cpp или отдельную систему сборки.

core владеет общими моделями; config — snapshot настроек; network — WinHTTP;
versions — перенесённые SemVer/cache; ui — taskbar/popup; integration — runtime
и lifecycle. accounts отсутствует до этапа 03. Ни данные, ни версии сейчас не
загружаются. Нет вымышленных процентов и отдельной диагностики переавторизации.
При создании/изменении границ папок актуализировать ближайшие AGENTS.md.
Компиляция и любые исполняемые проверки требуют отдельного разрешения.

