# История репозитория

Этот репозиторий изначально был запушен как ветка `gui-control-panel` из
монорепозитория **fantom-desktop** (GitFlic,
`git@gitflic.ru:ncbs-prog/fantom-desktop.git`) и первое время тянул за
собой всю историю монорепозитория целиком — включая бэкенд-скрипты
(`scripts/stream_ctl.sh`) и пресеты (`common/libptn_vid/examples/presets/`),
которым тут не место.

История была переписана: создана orphan-ветка без прошлого, в неё
перенесён только `tools/gui_control_panel/`. Старые ветки с полной
историей монорепозитория (`master`, `dev`, `developGalik*`, `ezcap`,
`gui-control-panel`) удалены отсюда — они остаются на GitFlic, тут их
никогда не было "по-настоящему".

## Где что искать

- **Бэкенд** (`stream_ctl.sh`, JSON-пресеты, деплой на удалённые машины) —
  только в fantom-desktop на GitFlic, ветки `gui-control-panel`/`ezcap`.
- **Этот репозиторий** — только GUI-инструменты. Каждый новый тул
  добавляется без переноса истории/бэкенда монорепозитория (см. память
  агента `fantom_desktop_github_tools_repo` в проекте fantom-desktop).

## Как перенести новую версию инструмента из монорепозитория

```sh
# в рабочей копии fantom-desktop (GitFlic), на ветке с актуальным тулом:
git show gui-control-panel:tools/gui_control_panel/src/main.cpp > /tmp/main.cpp
# скопировать актуальные файлы сюда вручную и закоммитить обычным коммитом —
# полную историю монорепозитория переносить не нужно.
```
