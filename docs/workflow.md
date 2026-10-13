# Рабочий процесс

## Установка (macOS, Apple Silicon)

```bash
tools/install_toolchain.sh
```

Скрипт идемпотентный, можно запускать повторно:
1. Ставит зависимости Homebrew, **включая `libmpc`**: без неё `cc1` падает с
   `Library not loaded: /opt/homebrew/opt/libmpc/lib/libmpc.3.dylib`.
2. Скачивает `pspdev-macos-latest-arm64.tar.gz` (~147 МБ) с докачкой (`curl -C -`) и проверяет архив `gzip -t`.
3. Распаковывает в `~/pspdev`, дописывает `PSPDEV` и `PATH` в `~/.zshrc`.
4. Собирает игру.

Проверка: `psp-gcc --version` (на 2026-10-13 это GCC 15.2.0).

## Сборка

```bash
psp-cmake -B build/psp          # конфигурация (один раз)
cmake --build build/psp         # сборка
```

Результат — готовая папка для карты памяти `build/psp/FuryOfTheSky/`:
```
EBOOT.PBP
assets/models/*.fsm, *.ftx
assets/textures/*.ftx
```
`create_pbp_file(... OUTPUT_DIR ...)` кладёт EBOOT прямо туда, пост-шаг копирует всю папку `assets`.

## Тесты симуляции на Mac

```bash
cmake -B build/host && cmake --build build/host && ctest --test-dir build/host
```
Обычный (не PSP) CMake собирает только `src/game` + `src/math` и `tests/`. 11 проверок модели полёта:
крейсер, форсаж, тормоз, набор высоты, перегрузка в вираже, крен, направления управления, сваливание,
падение в море, побитовый детерминизм, камера. Запускать после любой правки физики/камеры.

## Smoke-тест в эмуляторе (обязателен перед отдачей на железо)

```bash
tools/smoke_test.sh
```

Запускает сборку в PPSSPP из внутренней «карты памяти» эмулятора с файлом-флагом `autoshot`.
Игра летит по скрипту (горизонт → вираж с форсажем → набор высоты), делает 3 скриншота
(кадры 100, 220, 340), переходит в «Ангар», делает 4-й (кадр 420) и выходит. Скрипт сначала статически
проверяет ELF на `*ForKernel`, затем лог эмулятора на `Unknown syscall`, складывает PNG в `build/smoke/`.
Почему именно так — [psp/emulator.md](psp/emulator.md).

## Деплой на PSP

1. Вставить Memory Stick, запустить `tools/deploy.sh`.
2. Подключить кабель, на PSP: «Настройки → USB-соединение».
3. Скрипт ждёт карту до 2 минут, делает rsync в `PSP/GAME/FuryOfTheSky`, чистит
   AppleDouble-файлы `._*` (`dot_clean`), извлекает карту.
4. Выйти из USB-режима, запустить «Игра → Memory Stick».

На PSP нужна CFW (ARK-4 / PRO-C / LME на 6.61).

## Скриншоты с железа

**SELECT** в игре → `PSP/GAME/FuryOfTheSky/screenshots/shot_NNN.bmp` (480×272, 24 бит).

## Текстуры окружения

```bash
python3 tools/textures/env.py      # океан, облако, солнце → assets/textures/, превью build/previews/env_*.png
```

## Модели

```bash
/Applications/Blender.app/Contents/MacOS/Blender -b --factory-startup \
    --python tools/models/build.py -- su35s            # + превью
    ... -- su35s --no-preview                          # только ассеты
```

Сборка Су-35С занимает ~3 с без превью. Превью (`build/previews/<модель>_<ливрея>_<вид>.png`)
рендерятся из того же квантованного атласа, что уходит на PSP. После изменения модели —
пересобрать игру (ассеты копируются в папку для карты памяти при сборке).
