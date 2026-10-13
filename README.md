# Fury of the Sky

Аркадный воздушный бой для Sony PSP (homebrew). Между War Thunder и Ace Combat: без упора в реализм,
максимально зрелищно в пределах PSP. Целевое железо — PSP-3006 с CFW.

**Сейчас:** M0 «Ангар» — просмотрщик Су-35С (6 тыс. треугольников, запечённые текстуры, две ливреи).

## Быстрый старт (macOS, Apple Silicon)

```bash
tools/install_toolchain.sh     # pspdev в ~/pspdev + первая сборка
tools/smoke_test.sh            # проверка в эмуляторе (нужен: brew install --cask ppsspp)
tools/deploy.sh                # на PSP по USB
```

Управление: стик — камера, L/R — зум, △ — ливрея, ✕ — автовращение, SELECT — скриншот.

## Документация

Всё — решения, архитектура, особенности PSP, форматы — в [docs/](docs/README.md).
Правила проекта — [CLAUDE.md](CLAUDE.md).
