# Документация Fury of the Sky

Правила ведения документации — в [CLAUDE.md](../CLAUDE.md).

## Обзор
- [architecture.md](architecture.md) — устройство движка: модули, кадр, память, конвейер ассетов
- [workflow.md](workflow.md) — установка, сборка, деплой на PSP, smoke-тест, сборка моделей
- [roadmap.md](roadmap.md) — этапы разработки
- [devlog.md](devlog.md) — журнал по сессиям: что сделано, что сломалось и почему
- [story/world.md](story/world.md) — сюжет: ИИ PAX начал войну, чтобы её предотвратить; совет HELIOS позволил; персонажи
- [story/storytelling.md](story/storytelling.md) — как миссии рассказывают сюжет: каналы, журнал PAX, подготовки и отклики, все 35 записей
- [story/player.md](story/player.md) — сюжет глазами игрока: архив записей, обрыв записи, улики, нелинейность
- [story/campaign.md](story/campaign.md) — 35 записей по актам, хронометраж, карты, техника, порядок производства
- [story/cutscenes.md](story/cutscenes.md) — катсцены: графический роман, техника, список К00–К40
- [story/scenes.md](story/scenes.md) — диалоги ключевых сцен и журнал PAX
- [story/ending.md](story/ending.md) — финал по сценам
- [story/trilogy.md](story/trilogy.md) — трилогия: горизонт PAX в 50 лет, части II и III, перенос решений

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
- [0023 — трансзвуковое облако из клубов конденсата](decisions/0023-transonic-cloud-puffs.md)
- [0024 — звук: микшер, звуки самолёта, речевой информатор (фразы РИ-65)](decisions/0024-sound.md)
- [0025 — речевой информатор: реальные записи РИ-65](decisions/0025-real-ri65-voice.md)
- [0026 — вооружение: Р-73, Р-77 и ГШ-30-1 (подвеска, физика ракет, ЗРП, пушка, эффекты, звук)](decisions/0026-weapons-missiles-gun.md)
- [0027 — РЛС, ГСН и сигнатуры: реалистичное поведение наведения](decisions/0027-radar-guidance.md)
- [0028 — HUD прицеливания в стиле Ace Combat](decisions/0028-targeting-hud.md)
- [0029 — бессмертная тестовая цель](decisions/0029-test-drone.md)
- [0030 — управление v2: раскладка Ace Combat X, ПСМ «как в AC7»](decisions/0030-controls-ace-combat.md)
- [0031 — HUD: читаемость на экране PSP (подложки, размеры, толщина)](decisions/0031-hud-readability.md)
- [0032 — производительность: LOD самолётов, дешёвый свет, отсечение, замер кадра](decisions/0032-performance-lod.md)
- [0033 — противник: смертный бот с ИИ и три уровня сложности](decisions/0033-bot-ai-difficulty.md)
- [0034 — СПО (RWR): предупреждение об облучении и о ракетах](decisions/0034-rwr.md)
- [0035 — меню и интерфейс: металлический скин, растровый шрифт, стек экранов над живым ангаром](decisions/0035-menus-ui.md)
- [0036 — ангар меню: укрытие с запечённым освещением](decisions/0036-hangar-baked-lighting.md)
- [0037 — кампания, прогресс, ранги и профиль игрока (сохранение)](decisions/0037-campaign-progression-profile.md)
- [0039 — звания-шильдики, галерея эмблем с достижениями, магазин карточками, быстрый повтор](decisions/0039-rank-plates-emblems.md)
- [0038 — подвеска задания: загрузка пилонов, масса и сопротивление, конец задания, громкости](decisions/0038-loadout-stores.md)
- [0040 — море v2: кольца LOD без дыр, спектральная рябь, затенение на CPU](decisions/0040-ocean-v2.md)
- [0041 — погода и время суток вылета; объёмные облака, сплошная облачность, осадки](decisions/0041-weather-time-of-day.md)
- [0042 — архипелаг Северного моря: острова и кекуры как игровой рельеф, LOD-куски, программное отсечение](decisions/0042-sea-stacks-terrain.md)
- [0043 — граница района задания: предупреждение, затем игра разворачивает нос](decisions/0043-arena-boundary.md)
- [0044 — эффекты у воды: брызги от струй и даунвоша, следы на воде, всплески](decisions/0044-water-effects.md)
- [0045 — корабли: наземные цели, которые бьют только низко летящих](decisions/0045-ship-targets.md)
- [0046 — первые настоящие миссии: Северное море на архипелаге](decisions/0046-north-sea-missions.md)
- [0047 — целевое железо PSP-2000 и новее; планка — AAA-игры PSP своего времени](decisions/0047-target-psp2000-aaa.md)
- [0048 — сплошная облачность снизу: структура основания, клочья, горизонт](decisions/0048-overcast-from-below.md)
- [0049 — конвейер кадра (симуляция параллельно GE) и срез работы CPU](decisions/0049-frame-pipeline-cpu-budget.md)
- [0049 — HUD v3: гибрид «приборный + Ace Combat», растровый шрифт с обводкой, плашки предупреждений](decisions/0049-hud-v3.md)
- [0050 — постепенный захват: кольцо едет от носа к цели, при захвате — красный ромб](decisions/0050-gradual-lock-on.md)
- [0051 — рельеф: 16-битные вершины и свет, запечённый в цвет на каждую скалу](decisions/0051-terrain-compact-baked-vertices.md)
- [0052 — сюжетная кампания: одна страна, 34 миссии (заменён 0053)](decisions/0052-story-campaign-structure.md)
- [0053 — сюжет: мировая война в записях самописцев (заменён 0054)](decisions/0053-story-world-war-black-boxes.md)
- [0054 — сюжет v3: войну начал ИИ PAX; трилогия; катсцены — графический роман (предложено)](decisions/0054-story-pax-trilogy-cutscenes.md)

## Ассеты
- [assets/su35s.md](assets/su35s.md) — Су-35С: референсы, станции, подвижные части, подвеска, шасси
- [assets/hangar.md](assets/hangar.md) — укрытие для меню: размеры, бюджет, запечённый свет, камеры
- [assets/ui-art.md](assets/ui-art.md) — картинки локаций и схемы техники
- [assets/sound.md](assets/sound.md) — звуки, в т.ч. щелчки меню

## Знания о PSP
- [psp/gotchas.md](psp/gotchas.md) — ловушки железа, GE, прошивки и тулчейна (с пометкой, где проверено)
- [psp/hardware.md](psp/hardware.md) — характеристики, важные для движка, карта VRAM
- [psp/emulator.md](psp/emulator.md) — PPSSPP как инструмент отладки: логи, software renderer

## Форматы файлов
- [formats/fsm4.md](formats/fsm4.md) — меш с подвижными частями (текущий)
- [formats/fsm3.md](formats/fsm3.md) — меш, предыдущая версия (история)
- [formats/ftx2.md](formats/ftx2.md) — текстура (модели и окружение)
- [formats/ftr2.md](formats/ftr2.md) — меш скалы/острова: 16-битные вершины под запечённый свет (ADR 0051)
- [formats/ftr1.md](formats/ftr1.md) — меш скалы/острова (заменён FTR2, вход конвертера): дерево кусков с LOD, юбки, ленты, конусы нормалей
- [formats/wav.md](formats/wav.md) — звук: подмножество WAV, которое читает игра
- [formats/ffnt.md](formats/ffnt.md) — растровый шрифт меню: метрики, кегли, тексель «заливка + тень»
- [formats/profile.md](formats/profile.md) — профиль игрока (`profile.dat`): прогресс, ангар, настройки, CRC

## Ассеты
- [assets/su35s.md](assets/su35s.md) — Су-35С (с пилонами)
- [assets/r73.md](assets/r73.md) — ракета Р-73
- [assets/r77.md](assets/r77.md) — ракета Р-77
- [assets/rocks.md](assets/rocks.md) — скалы и острова Северного моря: референсы, бюджет, текстуры
- [assets/patrol_ship.md](assets/patrol_ship.md) — патрульный корабль «714»
- [assets/sound.md](assets/sound.md) — звуки и фразы речевого информатора
