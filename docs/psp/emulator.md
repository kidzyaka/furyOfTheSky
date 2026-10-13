# PPSSPP как инструмент отладки

Эмулятор — не целевая платформа ([ADR 0002](../decisions/0002-target-real-hardware.md)), но это единственный
способ получить логи загрузчика и HLE без PSPLink.

## Установка и запуск
```bash
brew install --cask ppsspp     # PPSSPPSDL.app
/Applications/PPSSPPSDL.app/Contents/MacOS/PPSSPPSDL --log=/abs/path/ppsspp.log -d path/to/EBOOT.PBP
```
- `-d` — уровень debug: видны импорты модулей, вызовы HLE, ошибки системных вызовов.
- Внутренняя «карта памяти»: `~/.config/ppsspp/PSP/` (`ms0:`). Конфиг: `~/.config/ppsspp/PSP/SYSTEM/ppsspp.ini`.

## Что искать в логе
| Строка | Значит |
|---|---|
| `Importing [UNK: 0x…]`, `Unknown syscall from known HLE module` | неразрешённый импорт; если модуль `*ForKernel` — см. [gotchas №1](gotchas.md) |
| `bad syscall instruction` | вызов неразрешённой заглушки — на железе здесь крах |
| `sceIoOpen(...)` с отрицательным результатом | файл не найден, проверить путь/рабочую папку |
| `Starting DL execution at … stall = …` | GE начал выполнять список команд (каждый кадр) |

## Скриншоты и чтение VRAM: только software renderer

- **Симптом.** Игра сохраняет скриншоты (CPU читает VRAM), а на них «застывший» кадр: одна и та же поза,
  поверх — наложение отладочного текста разных кадров. Хотя по логу кадр строится каждый раз.
- **Причина.** Аппаратный рендер PPSSPP (Vulkan/Metal) рисует на GPU Mac и **не копирует** кадр обратно
  в эмулируемую VRAM. CPU читает устаревшие данные. Флаг `--graphics=software` в SDL-сборке не применился.
- **Решение.** В `ppsspp.ini`, секция `[Graphics]`: `SoftwareRenderer = True`. `tools/smoke_test.sh`
  выставляет это сам.
- На железе такой проблемы нет: GE пишет прямо в VRAM.

## Хук `autoshot`
Если рядом с EBOOT лежит файл `autoshot`, игра (`main.cpp`) игнорирует джойстик, `FlightScene` летит
по скрипту (`scriptedInput`), скриншоты — на кадрах 100, 220, 340, на кадре 350 переход в «Ангар»,
скриншот на 420, выход на 430. На карту памяти PSP этот файл не копируется.

Software renderer PPSSPP эмулирует и логику отсечения граней GE — так проверялся выбор `GU_CCW`
([ADR 0012](../decisions/0012-backface-culling-ccw.md)).

## Чем эмулятор НЕ поможет
- Ошибки когерентности кэша (gotchas №2) — эмулятор их не воспроизводит.
- Производительность — FPS эмулятора ничего не говорит о железе.
- Тайминги GE, выравнивание данных — эмулятор терпимее железа.
