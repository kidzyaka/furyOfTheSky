# FSM3 — меш (Fury Static Mesh, версия 3)

> **Устарел 2026-10-14**, заменён [FSM4](fsm4.md) (таблица подвижных частей). Загрузчик FSM3 удалён; описание оставлено для истории.

Пишет: `MeshBuilder.export_fsm` (`tools/models/fmodel.py`). Читает: `gfx::Mesh::load` (`src/render/mesh.cpp`).
Решение — [ADR 0006](../decisions/0006-mesh-format-fsm3.md). Порядок байт — little-endian (как у PSP).

## Раскладка

| Смещение | Размер | Тип | Поле |
|---|---|---|---|
| 0 | 4 | char[4] | magic `FSM3` |
| 4 | 4 | u32 | `vertexCount` (≤ 65535) |
| 8 | 4 | u32 | `indexCount` (кратно 3) |
| 12 | 4 | f32 | `radius` — радиус ограничивающей сферы от начала координат модели, м |
| 16 | 32 × vertexCount | Vertex[] | вершины |
| … | 2 × indexCount | u16[] | индексы треугольников |
| … | 0–15 | — | нули до кратности 16 |

### Vertex (32 байта) = `GU_TEXTURE_32BITF | GU_NORMAL_32BITF | GU_VERTEX_32BITF`

| Смещение | Поле | Примечание |
|---|---|---|
| 0 | f32 u, f32 v | 0..1; v = 0 — первая строка текстуры |
| 8 | f32 nx, ny, nz | единичная нормаль, игровые координаты |
| 20 | f32 x, y, z | метры, игровые координаты ([ADR 0007](../decisions/0007-coordinates-units.md)) |

Порядок полей задан GE: texture → color → normal → position. Вершины и индексы выровнены на 16 байт
относительно начала файла (заголовок 16 байт, вершина 32 байта), файл грузится в `memalign(16)`.

Отрисовка: `sceGumDrawArray(GU_TRIANGLES, fmt | GU_INDEX_16BIT | GU_TRANSFORM_3D, indexCount, indices, vertices)`.

## История версий
- **FSM1** — неиндексированный, `GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF`, плоское затенение. Удалён.
- **FSM2** — неиндексированный, UV вместо цвета (атлас T4). Удалён.
- **FSM3** — индексированный, гладкие нормали. Заменён FSM4.
- **FSM4** — + таблица подвижных частей ([fsm4.md](fsm4.md)). Текущий.
