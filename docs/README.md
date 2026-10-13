# Документация Fury of the Sky

Правила ведения документации — в [CLAUDE.md](../CLAUDE.md).

## Обзор
- [architecture.md](architecture.md) — устройство движка: модули, кадр, память, конвейер ассетов
- [workflow.md](workflow.md) — установка, сборка, деплой на PSP, smoke-тест, сборка моделей
- [roadmap.md](roadmap.md) — этапы разработки
- [devlog.md](devlog.md) — журнал по сессиям: что сделано, что сломалось и почему

## Решения (ADR)
- [0000 — шаблон](decisions/0000-template.md)
- [0001 — тулчейн pspdev + CMake](decisions/0001-toolchain-pspdev-cmake.md)
- [0002 — целевое железо PSP-3006, эмулятор только для smoke-тестов](decisions/0002-target-real-hardware.md)
- [0003 — C++17 без исключений и RTTI](decisions/0003-cpp17-subset.md)
- [0004 — процедурные модели в Blender из кода](decisions/0004-procedural-models-blender.md)
- [0005 — запечённый текстурный атлас T8 с зеркальной развёрткой](decisions/0005-baked-t8-atlas.md)
- [0006 — формат меша FSM3: индексированный, float](decisions/0006-mesh-format-fsm3.md)
- [0007 — система координат и единицы](decisions/0007-coordinates-units.md)
- [0008 — настройка GU и кадра](decisions/0008-gu-frame-setup.md)
- [0009 — ассеты отдельными файлами рядом с EBOOT](decisions/0009-loose-asset-files.md)
- [0010 — аркадная модель полёта с фиксированным шагом](decisions/0010-arcade-flight-model.md)
- [0011 — отрисовка мира: небо, океан, облака, дымка](decisions/0011-world-rendering.md)
- [0012 — отсечение задних граней GU_CCW](decisions/0012-backface-culling-ccw.md)
- [0013 — рендер относительно камеры (матрицы GE 24-битные)](decisions/0013-camera-relative-rendering.md)
- [0014 — модель полёта v2: угол атаки, СДУ, сверхманевренность (кобра, кульбит)](decisions/0014-flight-model-v2-psm.md)
- [0015 — подвижные части самолёта: формат FSM4 и аниматор рулей](decisions/0015-articulated-mesh-fsm4.md)
- [0016 — визуальные эффекты: «режиссёр», частицы, ленты, пламя, звуковой барьер](decisions/0016-effects.md)
- [0017 — тепловые ловушки](decisions/0017-countermeasures.md)
- [0018 — HUD в стиле голографического индикатора](decisions/0018-hud.md)
- [0019 — освещение и графика: свет самолёта, море, блики, тень](decisions/0019-lighting-graphics.md)
- [0020 — трансзвуковая конденсация вместо «хлопка», физичный конденсат](decisions/0020-transonic-condensation.md)
- [0021 — HUD: компоновка между истребительным индикатором и Ace Combat](decisions/0021-hud-layout-v2.md)
- [0022 — ловушки: место выброса и баллистика](decisions/0022-flare-dispensers-v2.md)

## Знания о PSP
- [psp/gotchas.md](psp/gotchas.md) — ловушки железа, GE, прошивки и тулчейна (с пометкой, где проверено)
- [psp/hardware.md](psp/hardware.md) — характеристики, важные для движка, карта VRAM
- [psp/emulator.md](psp/emulator.md) — PPSSPP как инструмент отладки: логи, software renderer

## Форматы файлов
- [formats/fsm4.md](formats/fsm4.md) — меш с подвижными частями (текущий)
- [formats/fsm3.md](formats/fsm3.md) — меш, предыдущая версия (история)
- [formats/ftx2.md](formats/ftx2.md) — текстура (модели и окружение)

## Ассеты
- [assets/su35s.md](assets/su35s.md) — Су-35С
