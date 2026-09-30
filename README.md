# ЛР1 —– Геліостат

STM32 NUCLEO-C031C6, симуляція у Wokwi (заміна плати: Wokwi не підтримує моделі F411RE/F446RE, зазначені в методичці)

## Структура
- `firmware/` – проєкт, згенерований у CubeMX (USART2, ADC1 x4, TIM3 PWM x2, SPI1)
- `docs/report.md` – звіт про лабораторну роботу (журнал, проблеми, висновки)
- `docs/PRD.md`, `docs/hardware-components.md` – проєктна документація
- `docs/screenshots/` – скріншоти основних етапів налаштування та перевірки
- `wokwi.toml`, `diagram.json` – конфігурація симуляції Wokwi
- `Makefile` – `make build` / `make flash`

## Команди
- `make build` – компіляція прошивки (у папці firmware/) всередині Docker-контейнера з інструментарієм
- `make flash` – запуск симуляції Wokwi (без графічного інтерфейсу) зі свіжозібраною прошивкою