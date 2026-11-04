# HTTP-граница

http.h переиспользует async WinHTTP. Публичный Sub2ApiGet принимает snapshot,
относительный admin-ресурс и общий deadline, возвращает только data:object
при HTTP 2xx и envelope code=0/message:string. Нет POST/login/reset.
Одинаково поддерживать полный и ограниченный ключи. Не подавлять допустимую
серверную автоматику и не вызывать сброс самостоятельно.

x-api-key добавляется только для sub2api. GitHubGetReleases — отдельный вход
с фиксированным публичным URL без Settings/ключа. Не отправлять ключ в GitHub,
redirects, cookies или автоматическую авторизацию. Не выводить серверные
body/message/reason и заголовки в UI/логи; диагностика — событие/status/HRESULT.

Максимум 4 MiB, строгий UTF-8, минимум request timeout и общего deadline.
Не обновлять deadline между запросами одного refresh и не добавлять скрытые
повторы. Все ожидания видят cancel event; async state/readBuffer живут до
HANDLE_CLOSING. Возврат true не заменяет финальную проверку отмены перед
публикацией владельцем refresh. При смене настроек drain предшествует reset.
Runtime вызывает транспорт для списка и прямого quota. Необязательный выход
responseStatus передаёт только HTTP-статус для безопасной диагностики по local ID.
Сетевые проверки не проводились.

