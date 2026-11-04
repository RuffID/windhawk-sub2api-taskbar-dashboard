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

RefreshDashboardText передаёт один Settings/deadline в accounts loader и ждёт
всех quota workers. Publication gate проверяет cancel и generation под общим
mutex. Pause/unload меняют generation до drain. Отдельный age timer без HTTP
пересчитывает пригодность каждую секунду независимо от сетевого интервала;
его callbacks также останавливаются и drain выполняется до reset/settings.
Время успеха меняется только при полном discovery, успешных quota и пригодных
данных всех включённых подписок; пустой полный пул допустим. Частичный результат
и старение не обновляют время. Версии — этап 04; runtime-проверки ещё нужны.

