# OS-aware analyzers: рецепты MCP / WebAPI / CLI / Lua / Python

Применимо к обоим OS-aware анализаторам: `nedoos` (PLAN #78) и `dss` (PLAN #99).
Ниже везде `{os}` = `nedoos` | `dss`, `{id}` = UUID эмулятора. Требуется сборка
после `cd4b9857b` (merge обоих анализаторов): до неё MCP-инструмент
`analyze_performance` не знает op `analyzers`, а WebAPI отвечает
«Events not implemented».

## 1. MCP (порт 8092, streamable HTTP `POST /mcp`)

Инструмент `analyze_performance`, `action: "analyzers"`:

```jsonc
// список и статус
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "list"}}
// включить/выключить (ловушки ставятся после детекта ядра - на первом кадре)
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "enable", "name": "dss"}}
// поток событий (то же у nosedos)
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "events", "name": "dss"}}
// очистить кольцо
{"name": "analyze_performance", "arguments": {"action": "analyzers", "op": "clear", "name": "dss"}}
```

Снимок ОС (задачи/блоки/хэндлы) — у MCP-операции `analyzers` нет отдельного op
`state`, берётся generic-инструментом (всё, что есть в WebAPI, доступно так):

```jsonc
{"name": "invoke_api", "arguments": {"method": "GET",
  "path": "/analyzer/dss/events?include=snapshot&limit=5"}}
```

## 2. WebAPI (порт 8090)

```bash
# список анализаторов
curl -s http://localhost:8090/api/v1/emulator/$ID/analyzers
# включить
curl -s -X PUT http://localhost:8090/api/v1/emulator/$ID/analyzer/dss \
     -H 'Content-Type: application/json' -d '{"enabled":true}'
# события (последние N) + профиль/детект
curl -s "http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events?limit=50"
# то же со снимком: tasks[], blocks[] (страницы/блоки по владельцам), handles[]
curl -s "http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events?include=snapshot&limit=1"
# очистить
curl -s -X DELETE http://localhost:8090/api/v1/emulator/$ID/analyzer/dss/events
```

Схема события: `kind` (`OS_DETECTED|PROFILE_UNKNOWN|SYSCALL|TASK_START|TASK_EXIT|
PAGE_ALLOC|PAGE_FREE|FILE_OPEN|FILE_CLOSE|FINDING`), `severity`, `pc`, `caller`,
`task_id`, `syscall`, `message`, `detail`, `frame_number`, `formatted`.

Снимок NedoOS (с 2026-10-24) богаче: у задач `name` (первое слово командной строки
из страницы задачи), `stdin/stdout/stderr` (пайпы 0x80+) и `last_time` (кванты
ядра; относительная доля = CPU% - CLI `analyzer nedoos state` печатает его),
плюс `pipes[]` (стороны/читатель/строки/байты) и `handles[]` из `ffilearray`
(открытые FAT-файлы с владельцем, позицией и размером). Пример живого снимка
(idle → term.com → cmd.com, пайпы скрещены): task 2 'term.com' io=#80/#81,
task 3 'cmd.com' io=#81/#80, pipe #80 reader=2, pipe #81 reader=3.

## 3. CLI (telnet :8765)

```
analyzer dss enable
analyzer dss events --limit=100
analyzer dss state        # задачи, владельцы блоков, файловые хэндлы
```

## 4. Lua / Python

```lua
analyzer_enable("dss")
local ev = analyzer_events("dss", 100)
local st = analyzer_state("dss")   -- tasks/blocks/handles
```

```python
em.analyzer_events("dss", limit=100)
em.analyzer_state("dss")
em.analyzer_clear("dss")
```

## 5. Готовые сценарии

### 5.1 Поднять Sprinter с DSS и посмотреть задачи/память (полный флоу)

```bash
# создать машину и вставить диск (образ строит scratch/osaware-live/build_dss_hdd.py по спеке
# BuildDssHdd из core/tests/emulator/machines/sprinter/sprinterdssmedia.h)
ID=$(curl -s -X POST http://localhost:8090/api/v1/emulator/create \
     -H 'Content-Type: application/json' -d '{"model":"SPRINTER","ram":4096}' \
     | python3 -c 'import json,sys;print(json.load(sys.stdin)["id"])')
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/media/ide0.master/insert \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/osaware-live/dss171-session.img","access":"session"}'
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/reset -d '{}' >/dev/null
curl -s -X POST http://localhost:8090/api/v1/emulator/$ID/start >/dev/null   # НЕ /emulator/start с id в теле - он создаёт новый инстанс
sleep 15                                                                    # загрузка DSS в реальном времени
# дальше - MCP-вызовы из §1 (enable -> events -> snapshot)
```

NedoOS: то же с `{"model":"ATM3"}` и слотом `sd.zc` (host-папка с sdcard-фикстурой,
`InsertOptions.freeBytes >= 16 МБ`), после загрузки — `analyzer nedoos …`.

### 5.2 «Крикнуть в консоль, если память ОС не отдали»

1. `analyzer {os} enable` до запуска тестируемой программы.
2. Снять базу: `GET .../events?include=snapshot` (запомнить `blocks`/`handles`).
3. Запустить программу в гостевой ОС, дать ей завершиться.
4. `GET .../events?limit=200` и отфильтровать `kind=FINDING`:
   - NedoOS: `task N exits owning K page(s)` — задача вышла, владея страницами
     (ядро почистит, приложение забыло `CMD_DELPAGE`);
   - DSS: `task N exits owning block(s)` (забыт `FREEMEM`) и
     `task N exits leaving file handle(s) open` (класс C: LEAVE файлы не закрывает).
   Каждый finding несёт `pc`/`caller` — адрес виновника.

### 5.3 Профиль «прожорливости» программы

- поток: `PAGE_ALLOC`/`PAGE_FREE` по `task_id` → сколько страниц держала задача
  в каждый момент (пик = max одновременно живущих);
- `blocks` из snapshot после выхода из программы ≠ база → безвозвратный лик
  (BIOS-уровень, класс B: alloc прошёл мимо MEMTAB);
- DSS `handles[]` из snapshot → незакрытые файлы по именам.

## 6. Подводные камни

- Анализаторы **не активны по умолчанию** и не мешают машинам без своей ОС:
  на «чужой» машине детект молчит (событий нет, ошибки нет).
- Детект стреляет на первом кадре после enable: события идут только после
  `OS_DETECTED`; на неизвестном билде — `PROFILE_UNKNOWN` (векторный режим:
  поток вызовов без структур/аудита).
- Горячие вызовы (NedoOS YIELD/PRCHAR, DSS WAITKEY/PUTCHAR/SETWIN-маппер #C4)
  не попадают в кольцо — считаются счётчиком, чтобы не вытеснять полезное.
- Тесты/автоматизация, гоняющая кадры напрямую, должна использовать
  `RunFrame(false)` (без skip-breakpoints), иначе ловушки не сработают.
- Автоматизация, меняющая эмулятор без `take_control`, получает implicit-сессию —
  для длительных прогонов возьмите контроль (`POST /control/session`, MCP
  `take_control`) и отпустите после.
