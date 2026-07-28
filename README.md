# fantom-desktop control panel

Мини-GUI на Dear ImGui для управления потоками видео-релея fantom-desktop
(источник `ezcap`/`encoder`/`webcam` → приёмник `vrx188`/`nsu212`) без
необходимости держать открытым терминал с SSH-командами.

## Сборка

```sh
cd tools/gui_control_panel
cmake -S . -B build
cmake --build build -j$(nproc)
./build/gui_control_panel
```

Зависимости: SDL2 (через pkg-config), OpenGL. Dear ImGui подтягивается
автоматически через CMake `FetchContent` — устанавливать отдельно не нужно.

## Что делает

- Дропдауны источник (Ezcap / Энкодер / Webcam) и приёмник (.188 / nsu212).
- Кнопки **Старт**/**Стоп**/**Статус** вызывают `scripts/stream_ctl.sh` из
  основного репозитория fantom-desktop по SSH на удалённых машинах —
  сам скрипт сюда не входит, см. [MIGRATION.md](MIGRATION.md).
- Окно всегда поверх остальных (`ALWAYS_ON_TOP`), сворачивается до полоски
  с названием, запоминает позицию на экране между запусками, есть
  XDG-автозапуск при входе в графическую сессию.

## Автозапуск

Шаблон `.desktop`-файла лежит в `tools/gui_control_panel/gui_control_panel.desktop`
— скопировать в `~/.config/autostart/`, чтобы панель поднималась при логине.
