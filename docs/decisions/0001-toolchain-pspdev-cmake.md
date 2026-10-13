# 0001 — Тулчейн pspdev + CMake

- **Статус:** Принято
- **Дата:** 2026-10-13

## Контекст
Нужен поддерживаемый компилятор под MIPS Allegrex и SDK PSP, работающий на macOS (Apple Silicon).

## Решение
- **pspdev** (github.com/pspdev/pspdev), готовая сборка `pspdev-macos-latest-arm64.tar.gz` из rolling-релиза `latest`,
  установка в `~/pspdev`. На 2026-10-13: GCC 15.2.0, newlib, pspsdk.
- Система сборки — **CMake** через обёртку `psp-cmake`, упаковка в EBOOT макросом `create_pbp_file`
  (`~/pspdev/psp/share/CreatePBP.cmake`).
- Линкуем только нужные библиотеки SDK: `pspgum pspgu pspge pspdisplay pspctrl psprtc pspdebug m`.
  Пользовательские заглушки ядра (`libpspuser`) тулчейн подключает сам.
  **`pspkernel` не линковать** — см. [gotchas №1](../psp/gotchas.md).
- Флаги: `-O2 -G0`, для C++ `-fno-exceptions -fno-rtti -fno-threadsafe-statics` (см. [0003](0003-cpp17-subset.md)).

## Рассмотренные альтернативы
- Классический Makefile pspsdk (`build.mak`) — работает, но CMake удобнее для роста проекта и IDE.
- Старые тулчейны (minpspw, GCC 4.x) — заброшены, нет C++17.
- Сборка pspdev из исходников — долго, нужно только при отсутствии бинарного релиза.

## Последствия
- Rolling-релиз может поменяться: при проблемах после переустановки сверять версию GCC.
- Бинарники для macOS динамически линкуются с библиотеками Homebrew (`gmp`, `mpfr`, `libmpc`, `isl`…):
  список зависимостей держим в `tools/install_toolchain.sh`.
