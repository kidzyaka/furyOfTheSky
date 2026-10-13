# Архитектура

Состояние: **M1 «Полёт» — тестовая сцена** (полёт над бесконечным океаном) + сцена «Ангар» из M0.
Документ описывает то, что есть в коде сейчас; планы — в [roadmap.md](roadmap.md).

## Структура репозитория

```
CMakeLists.txt            PSP-сборка (psp-cmake) ИЛИ host-тесты (обычный cmake)
CLAUDE.md                 правила проекта, в т.ч. документации
src/
  main.cpp                загрузка, цикл кадра, переключение сцен, меню паузы, FPS, autoshot
  math/                   Vec3, Quat (header-only, платформенно-нейтральные)
  game/                   СИМУЛЯЦИЯ, без заголовков PSP:
    controls.h              ControlInput — намерение пилота на шаг
    aircraft_params.h       параметры модели полёта (Су-35С)
    flight_model.*          аркадная модель полёта (ADR 0010)
    chase_camera.*          камера «на поводке»
    world.h                 солнце, дымка, стартовые условия
  platform/               ОС PSP: callbacks/выход, таймер, 333 МГц, джойстик
  core/file.*             загрузка файла в выровненную память + writeback кэша
  render/                 GE/GU:
    gu_context.*            VRAM, init, кадр, 2D-градиент
    view.*                  матрицы (проекция/вид/модель из кватерниона), свет, туман
    mesh.*, texture.*       FSM3, FTX2
    sky.*, ocean.*, clouds.* мир (ADR 0011)
    screenshot.*            BMP с кадра
  scenes/                 экраны игры: Scene, Assets, Settings, FlightScene, HangarScene
tests/                    host-тесты симуляции (clang, ctest)
assets/                   СГЕНЕРИРОВАННЫЕ ассеты — не править руками
  models/                   su35s.fsm, su35s_<ливрея>.ftx
  textures/                 ocean.ftx, cloud.ftx, sun.ftx
tools/
  pipeline/               общий код: ftx.py (квантизация, мипы, swizzle), png.py
  models/                 генератор моделей и запекание (Blender)
  textures/env.py         текстуры окружения (numpy)
  install_toolchain.sh, deploy.sh, smoke_test.sh
docs/                     документация
```

## Слои и зависимости

```
main ──► scenes ──► game ──► math        (game и math компилируются и на Mac)
            │
            ├──► render ──► core
            └──► platform
```

- `game` и `math` **не включают ни одного заголовка PSP** — это граница, по которой проходят host-тесты и
  будущий сетевой код. Состояние симуляции (`FlightState`) — простые данные, отдельно от отрисовки.
- `render` ничего не знает об игровой логике; сцены связывают `game` с `render`.
- `scenes::Scene` — интерфейс экрана (`enter / update / render / overlay`), виртуальные функции без RTTI.

## Потоки

| Поток | Приоритет | Назначение |
|---|---|---|
| `user_main` | 0x20 | всё: ввод, симуляция, построение списка команд GE |
| `callbacks` | 0x11 | спит в `sceKernelSleepThreadCB`, принимает exit callback от HOME |

## Кадр

```
readPad                      ввод (мёртвая зона стика 22%)
START → пауза / меню         в паузе обновляется меню, сцена стоит
scene.update(pad, dt)        FlightScene: аккумулятор, шаги симуляции по 1/60 с (≤ 5 за кадр),
                             камера обновляется внутри шага
beginFrame                   clear цветом дымки + глубина
scene.render()               FlightScene: небо+солнце → туман → океан → самолёт (свет) → облака
endFrame                     sceGuFinish → sceGuSync → overlay (HUD, FPS, меню, скриншот) → vsync → swap
```

CPU ждёт GE каждый кадр (последовательно). Частота CPU выставляется в 333/166 МГц при старте
(`platform::setMaxClock`); если не удалось — в углу HUD красное «222!».

## Память

### VRAM (2 МБ)
| Диапазон | Размер | Содержимое |
|---|---|---|
| `0x000000–0x087FFF` | 544 КБ | кадровый буфер 0, 512×272, 8888 |
| `0x088000–0x10FFFF` | 544 КБ | кадровый буфер 1 |
| `0x110000–0x153FFF` | 272 КБ | глубина 16 бит |
| `0x154000–0x1FFFFF` | 688 КБ | свободно |

### Основная память
- Куча: `PSP_HEAP_SIZE_KB(-1024)` + `MEMSIZE=1`.
- Список команд GE: статический 1 МБ (кадр M1 — порядка нескольких КБ + спрайты облаков через `sceGuGetMemory`).
- Ассеты (`scenes::Assets`, грузятся при старте): меш Су-35С ~190 КБ, 2 ливреи по 341 КБ, океан 86 КБ,
  облако и солнце по 6 КБ.
- Статическая геометрия мира в объектах сцен (сцены — `static` в `main`, не на стеке):
  небо ~6 КБ, океан ~34 КБ, облака ~16 КБ. После заполнения — writeback кэша.

## Управление (FlightScene)

| Ввод | Действие |
|---|---|
| Стик | тангаж (на себя — нос вверх; инверсия в меню) / крен |
| L / R | рыскание |
| △ держать | полная тяга → форсаж |
| ✕ держать | малый газ + воздушный тормоз |
| START | пауза: сцена, ливрея, инверсия тангажа, рестарт |
| SELECT | скриншот |

## Конвейер ассетов

```
tools/models/<самолёт>.py ─(Blender)─► fmodel.py ─► assets/models/*.fsm, *_<ливрея>.ftx
tools/textures/env.py ─(numpy)────────────────────► assets/textures/*.ftx
                    оба используют tools/pipeline/ftx.py (k-means / ручная CLUT → мипы → swizzle)
```
Подробно — ADR [0004](decisions/0004-procedural-models-blender.md), [0005](decisions/0005-baked-t8-atlas.md),
[0011](decisions/0011-world-rendering.md); форматы — [formats/](formats/).

## Системы координат
[ADR 0007](decisions/0007-coordinates-units.md): игра — X вправо, Y вверх, −Z вперёд, метры.
Связанные оси самолёта: угловые скорости `x` — тангаж (нос вверх), `y` — рыскание (нос влево), `z` — крен влево.
Матрицы pspgum — по столбцам: `x/y/z` — образы осей, `w` — перенос (`gfx::setModel`).
