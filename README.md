# openspore-mod

Мультиплеер для **Spore: Galactic Adventures** (космическая стадия, 2 игрока, P2P, хост
авторитетен). Клиентская часть проекта [OpenSpore](https://github.com/OpenSpore); сервер лобби —
[openspore-server](https://github.com/OpenSpore/openspore-server); реверс и адреса —
[idapro-reverse-dump](https://github.com/OpenSpore/idapro-reverse-dump).

Полный отчёт о ресёрче (prior art, SDK, протокол, план экспериментов):
`idapro-reverse-dump/docs/MULTIPLAYER-RESEARCH.md`.

## Что здесь

```
src/net/        переносимое сетевое ядро: протокол (OSMP v1), UDP-сессия (handshake, reliable/
                unreliable каналы, keepalive), интерполяция, реестр NetId    -> тесты на Linux
src/game/       EngineFacade (интерфейс к игре), Replication (M3/M4: ghost-корабль второго
                игрока, frame-aware), GhostLab (локальный эксперимент без сети)
src/lab/        osmp_lab.dll - лабораторная DLL БЕЗ ModAPI: сырые адреса Steam/GOG 3.1.0.29,
                vtable-хук cSimulatorSpaceGame::Update, управление marker-файлами. Собирается
                mingw (Linux/WSL) или MSVC. Плюс inject.exe.
src/modapi/     OpenSporeMP - настоящий мод на Spore ModAPI SDK (VS2022): чит-команды
                mpHost/mpJoin/mpGhostTest/..., поддержка двух копий игры на одном ПК
tools/netlab    osmp_netlab.exe - проверка транспорта между двумя машинами без игры
tests/          27 unit-тестов (протокол, сессия через loopback UDP, интерполяция, репликация
                с FakeEngine) - `cmake . && make && ./osmp_tests`
```

Три реализации одного интерфейса `IEngine` (FakeEngine / RawEngine / ModApiEngine): логика
репликации одна, тестируется без игры, а в игре подменяется только слой доступа к движку.

## Быстрый старт: лабораторная DLL (не нужен ModAPI)

```bash
# Linux/WSL: apt install mingw-w64
./src/lab/build-mingw.sh            # -> build-win32/osmp_lab.dll, inject.exe, osmp_netlab.exe
# Windows (x86 Native Tools / VS2022): src\lab\build.cmd
```

1. Запустить Spore GA обычным способом, дойти до космической стадии.
2. `inject.exe SporeApp.exe C:\full\path\osmp_lab.dll` (тот же инжектор, что в m0).
3. Рядом с DLL появятся `osmp_lab.log` и `osmp_status.txt`. Управление - файлы рядом с DLL:

| файл | содержимое | действие |
|---|---|---|
| `osmp_ghosttest` | пусто | **главный эксперимент**: спавнит ghost-корабль через `Simulator::SpawnUFO` и водит его по кругу вокруг игрока |
| `osmp_mode.txt` | `moveto` / `teleport` / `setpos` / `velocity` / `raw` | способ управления ghost'ом (гипотезы M3) |
| `osmp_ghostkill` | пусто | убрать тестовый ghost |
| `osmp_host.txt` | `7777` | хостить сессию на UDP-порту |
| `osmp_join.txt` | `192.168.1.10 7777` | присоединиться |
| `osmp_leave` | пусто | выйти |
| `osmp_dump` | пусто | описать корабль игрока в лог |

Лог каждую секунду пишет `ghostlab[mode]: target (...) actual (...) err=...` - по `err` видно,
слушается ли движок в данном режиме. Файлы-команды удаляются после обработки.

Адреса - только для Steam/GOG 3.1.0.29 (семейство «march2017»); при несовпадении DLL делает
self-test и отказывается ставить хук.

## ModAPI-мод (OpenSporeMP)

Требования: Visual Studio 2022 (C++ workload, v143, Win10 SDK), [Spore ModAPI SDK](https://github.com/emd4600/Spore-ModAPI),
[ModAPI Launcher Kit](http://davoonline.com/sporemodder/rob55rod/ModAPI/Public/).

1. Отредактировать `src/modapi/SdkPathConfig.props` (пути к SDK и Launcher Kit).
2. Открыть `src/modapi/OpenSporeMP.sln`, собрать Release|x86 -> DLL кладётся в `mLibs` лаунчера.
3. Запустить игру через ModAPI Launcher, в консоли (`Ctrl+Shift+C`):

```
mpGhostTest              # локальный ghost без сети, затем mpGhostMode teleport / velocity / ...
mpHost 7777              # на одной машине
mpJoin 192.168.1.10 7777 # на другой
mpStatus                 # что происходит
```

Две копии игры на одном ПК: вторую запускать с переменной окружения `OSMP_PROFILE=2`
(отдельные папки профиля + переименованный single-instance mutex; техника SporeCoop).

Код мода проверяется здесь только синтаксически против заголовков SDK (GCC с патчем макросов);
линковка и запуск - на Windows.

## Проверка транспорта без игры

```
osmp_netlab host 7777                 # машина A
osmp_netlab join <ip A> 7777          # машина B
```

Печатает RTT, потери, ресенды и позицию «ghost'а» второй стороны. Если это работает - сеть
настроена (порт проброшен / VPN), и можно идти в игру.

## Протокол (OSMP v1)

Заголовок 12 байт: magic `OSMP`, версия, тип, флаги (reliable), seq, ack. Сообщения:
`Hello/Welcome/Reject/Bye` (reliable), `Ping/Pong/Ack`, `EntitySpawn/EntityDespawn/FrameChange`
(reliable), `Transform` (unreliable, sequenced). Каждая позиция несёт **frame**
`{GALAXY|SYSTEM(starId)|PLANET(planetId)}` - без него координаты в Spore бессмысленны
(измерено в idapro-reverse-dump). `NetId = owner << 24 | counter`, выдаёт владелец.

## Статус

- [x] M2 транспорт + протокол (тесты)
- [x] M3/M4 код репликации + интерполяция (тесты с FakeEngine); в игре **не проверено** -
      нужен запуск на Windows с Spore: см. план экспериментов в MULTIPLAYER-RESEARCH.md
- [ ] M5 ownership/ввод, M6 бой/инструменты, M7 NPC, M8 планеты/системы, M9 галактика
