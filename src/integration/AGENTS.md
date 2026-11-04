# Интеграция и lifecycle

lifecycle.h — четыре entrypoints и порядок cleanup; runtime.h — timers,
manual refresh work и пауза; windows.h — hooks и dispatch на UI-потоки;
xaml_tap.h — собственный COM CLSID, watcher, advise/unadvise; cleanup дожидается
завершения самого advise-потока до освобождения его handle и объекта watcher.
VisualDiagConnectionN — внешнее имя протокола Windows, не ID мода.

Сначала paused/unloading + cancel, затем дождаться timer/manual callbacks,
остановить recovery, отписать watcher, удалить свои UI callbacks/widgets и
popup на владеющих потоках, снять hook/класс, закрыть event. Ошибка одного
cleanup шага не должна пропускать независимые шаги. Не сбрасывать cancel до
drain старых читателей. При ошибке settings не продолжать старое обновление.
Не публиковать результат отменённой работы.

На этапе 02 RefreshDashboardText только сообщает состояние подключения:
аккаунты пустые, процент неизвестен, lastUpdated — прочерк. Не подключать
данные/версии до поручения следующих этапов. Runtime-проверки ещё нужны.

