# Postgresql_Connection_Pool
Header-only C++ multi-thread connection pool for PostgreSQL

# Быстрый старт
```cpp
#include "Postgresql_Connection_Pool.h"

// Создание пула соединений
ConnectionPool pool("host=localhost dbname=mydb user=user password=pass");

// Синхронный запрос с результатом
auto result = pool.request("SELECT * FROM users WHERE age > {}", 18);
for (const auto& row : result) {
    std::cout << row["name"].as<std::string>() << std::endl;
}

// Асинхронный запрос
size_t task_id = pool.request_async("SELECT * FROM posts LIMIT {}", 10);
// ... делаем другую работу ...
auto posts = pool.get_result_async(task_id);

// Команда без результата (fire-and-forget)
pool.edict("UPDATE stats SET views = views + 1");
```
# Расширенные возможности
* Сбор статистики
```cpp
ConnectionPool<true, true> pool("..."); // Включить подсчет запросов и мониторинг потоков 
// При разрушении пула выведет статистику
```
* Отключение health-check
```cpp
ConnectionPool<true, false> pool("..."); // Включает подсчет и отключить мониторинг потоков
```
# Особенности
* 🚀 Автоматическое управление соединениями

* 🧵 Потокобезопасность из коробки

* ⚡ Sync/Async API

* 🩺 Health-check потоков (можно отключить)

* 📊 Сбор статистики (можно отключить)

* 🏎️ Compile-time оптимизации через if constexpr

# Требования
1. C++20 (для std::format)

2. libpqxx

3. PostgreSQL
