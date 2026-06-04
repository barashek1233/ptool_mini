# ptool_mini

Консольная утилита для прошивки устройств «куб» (aQsi) с чипами **Atmel SAMA5D2** и **Allwinner**. Запускается без GUI, прошивает одно устройство и завершается с кодом 0 (успех) или ненулевым (ошибка).

## Использование

```bash
ptool_mini --cpu atmel     --image-path /path/to/images/
ptool_mini --cpu allwinner --image-path /path/to/image.img
```

## Стек

- **C++17**, **Qt 5** (Core, Network)
- **CMake** + **Ninja**, сборка: `cmake -B build && cmake --build build`
- **CPack** (DEB): `cmake --build build --target package`
- Внешние зависимости подтягиваются через `FetchContent` при сборке:
  - `sam-ba` — утилита Microchip/Atmel для прошивки через SAM-BA Monitor
  - `liballwinner-flashing-helper` (`cube_aw_flash`) — библиотека для прошивки Allwinner через FEL/USB

## Архитектура

### Точка входа — `main.cpp`

Два режима запуска:
- `ptool_mini --sam-ba <args>` — запускает встроенный `SambaTool` (подрежим, вызывается самим ptool_mini как подпроцесс)
- `ptool_mini --cpu <type> --image-path <path>` — основной режим прошивки

В режиме `--sam-ba` ищет QML-файлы и метаданные последовательно по кандидатам:
```
<app_dir>/lib/qml
<app_dir>/qml
<app_dir>/../lib/qml        ← /usr/lib/qml  (установка через .deb)
<app_dir>/../lib/ptool/qml  ← /usr/lib/ptool/qml  (совместимость со старым ptool)
```

### Atmel-прошивка (`--cpu atmel`)

```
udev (libudev)
  └─ DevNotifyQtStyle          — слушает hotplug-события USB ttyACM
       └─ Mapping               — назначает стабильный номер кабеля по USB-пути
            └─ CubeFlasherManager — управляет жизненным циклом прошивальщиков,
                                    планирует повторные попытки (до 2 мин)
                 └─ CubeFlasher  — один прошивальщик на устройство
                      ├─ HardwareDetection  — запускает sam-ba для чтения 4 регистров
                      │                       (0xFC03800{8,48,88,C8}), определяет
                      │                       HW-ревизию (cube_t / cube_d / Unknown)
                      └─ QProcess            — запускает sam-ba с командами
                                               bootconfig → erase → write → verify
```

**Файлы образа** (ожидаются в `--image-path`):
- `*.ubi` / `*.ubifs` — rootfs
- `u-boot.bin`
- `at91bootstrap.bin`
- `bootconfig.txt` (опционально; без него используются дефолтные команды bootconfig)

**HW-ревизии** (`HardwareDetection::Revision`): `kCube_T_b_r8_Lite`, `kCube_T_b_r7_Gemalto`, `kCube_T_b_r8_Quectel`, `kCube_T_b_r8_Cam_Quectel`, `kCube_d_r16_Cam_Gemalto`, `kCube_d_r17`, `kCube_d_r17_Gemalto`, `kCube_d_r17_Quectel`. При `kUnknown` — продолжает с первым доступным образом.

**sam-ba аргументы** (пример):
```
ptool_mini --sam-ba -p serial:ttyACM0 -d sama5d2 -a nandflash:2:8:0xc1807007 -c erase
```

### Allwinner-прошивка (`--cpu allwinner`)

`AllwinnerDevicesManager` использует `cube_aw_flash` (C-библиотека). Устройства обнаруживаются поллингом. Прошивка выполняется в `QThreadPool`.

### Ключевые классы

| Файл | Назначение |
|---|---|
| `main.cpp` | Точка входа, маршрутизация режимов |
| `CubeFlasherManager` | Оркестрация прошивки, retry-логика |
| `CubeFlasher` | Прошивка одного Atmel-устройства через sam-ba |
| `HardwareDetection` | Определение HW-ревизии через sam-ba read32 |
| `ConsoleAtmelFlasher` | Обработка сигналов завершения, код возврата |
| `DevNotifyQtStyle` | Hotplug USB через libudev |
| `Mapping` | USB-путь → номер кабеля (стабильная нумерация) |
| `CubeImageInfo` | Парсинг имён файлов образов |
| `ConfigManager` | QSettings (`~/.config/ptool.conf`, INI) |
| `AllwinnerDevicesManager` | Управление Allwinner-устройствами |

## Packaging (.deb)

Устанавливает:
- `/usr/bin/ptool_mini`
- `/usr/lib/qml/SAMBA/...` — QML-модули sam-ba
- `/usr/lib/metadata/` — `connection_serial.json`, `device_sama5d2.json`
- `/usr/lib/libsamba_conn_xmodem.so`, `/usr/lib/libcube_aw_flash.a`
- `/usr/lib/udev/rules.d/99-ptool-mini.rules`

`postinst` устанавливает `cap_sys_admin,cap_sys_rawio+ep` на бинарь (для Allwinner) и создаёт группу `ptool`.

## udev-правила (`packaging/99-ptool-mini.rules`)

Устройство Atmel SAM-BA: VID `03eb`, PID `6124` (`/dev/ttyACM*`).
Правила включают `ENV{ID_MM_DEVICE_IGNORE}="1"` и `ENV{BRLTTY_BRAILLE_DEVICE_IGNORE}="1"` — необходимо на Ubuntu 22.04, где `brltty` захватывает ACM-устройства с VID Atmel.

Allwinner (FEL-режим): VID `1f3a`.

После изменения правил применять:
```bash
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Известные особенности

- **Ubuntu 22.04**: `brltty` захватывает `/dev/ttyACM0` — решается udev-правилами выше или `sudo apt remove brltty`.
- **sam-ba не находит QML**: если бинарь в `/usr/bin/`, QML должен быть в `/usr/lib/qml/` (кандидат `../lib/qml`). Старый ptool использовал `/usr/lib/ptool/qml/`.
- **`XDG_RUNTIME_DIR`**: на серверах без сессии переменная может отсутствовать — `main.cpp` создаёт `/tmp/runtime-<uid>` автоматически.
- Повторные попытки прошивки — каждые 5 с, не более 2 мин.
- HW-детекция с таймаутом 3 с (настраивается через QSettings `timeoutForHardwareDetection`).
