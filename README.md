# TDKR Bridge

**English:** run **The Dark Knight Rises 1.1.6** (Gameloft, 32-bit ARMv7, Amazon build `GloftKRAS`) on
64-bit-only Android phones that have no AArch32 support. The game runs under a patched `qemu-user`, and its
OpenGL ES calls are forwarded to the phone's real GPU. Tested on a OnePlus 13s (Snapdragon 8 Elite): 100-120 fps in
the first mission.
No game files are included: you need your own copy. The rest of this page is in Russian.

Запуск 32-битной **The Dark Knight Rises** (Gameloft, v1.1.6, только `armeabi-v7a`) на 64-битных
Android-телефонах, в которых нет поддержки AArch32.

Игра исполняется в пропатченном `qemu-arm` (linux-user), а её вызовы OpenGL ES пробрасываются на настоящий
GPU телефона. Способ тот же, что в NOVA3 Bridge, Shadowgun Bridge и Mass Effect Infiltrator Bridge; за основу
взят NOVA3 Bridge (тот же движок Gameloft).

**Игра не распространяется.** Нужны ваши собственные APK версии 1.1.6 и кэш к ней.
Готовая сборка: [Releases](https://github.com/JamesCrumble/the-dark-knight-rises-bridge/releases).

[Состояние](#состояние) · [Установка](#установка) · [Настройки](#настройки) · [Как это устроено](#как-это-устроено) ·
[Производительность](#производительность) · [Структура](#структура) · [Сборка](#сборка) · [Диагностика](#диагностика) ·
[Другие игры](#другие-игры) · [Лицензия и авторство](#лицензия-и-авторство)

## Состояние

- Игра запускается на OnePlus 13s (Snapdragon 8 Elite, Android 15): меню, первая миссия, бой.
- Скорость при лимите 120: меню 120 fps, первая миссия 100–120 fps.
- Касания работают. Геймпад не проверялся: раскладка кнопок осталась от N.O.V.A. 3.
- Звук: игра открывает звуковой выход и пишет в него; на слух отдельно не проверялся.
- Проверено на одном устройстве. Долгая игра и остальные миссии не проверялись.

## Установка

1. Установите `TDKR-Bridge-<версия>.apk` со страницы Releases (пакет `com.jamescrumble.tdkr`).
2. Запустите приложение один раз и закройте: появится папка `/sdcard/Android/data/com.jamescrumble.tdkr/files/`.
3. Положите в неё два файла как есть: оригинальный APK игры 1.1.6 и zip-архив с её кэшем. Имена не важны.
   Приложение само распакует архив при первом запуске игры. Архив должен быть именно zip: кэш в формате 7z
   нужно сначала перепаковать в zip либо распаковать вручную (см. ниже).
4. Откройте приложение, при необходимости поменяйте настройки и нажмите «Играть».

Распаковка вручную: папку `com.gameloft.android.AMAZ.GloftKRAS` из архива кэша (внутри `files/data`,
`files/textures`) положить в ту же папку целиком. Если копировали по adb, откройте папки приложению
(adb создаёт их от имени `shell`):
`adb shell "find /sdcard/Android/data/com.jamescrumble.tdkr/files/com.gameloft.android.AMAZ.GloftKRAS -type d -exec chmod 2777 {} +"`

Из папки проекта ручную раскладку делает `tools/push_data.sh`. Если чего-то не хватает, причина пишется на экране и в
`tdkr.log` в папке с файлами игры. Первый запуск дольше: распаковывается sysroot (~100 МБ).

## Настройки

Экран настроек открывается при старте приложения. Значения хранятся в `settings.txt` рядом с файлами игры.

| Настройка | Ключ | Что делает |
|---|---|---|
| Отступ интерфейса от краёв | `TDKR_HUD_INSET` | 0–12 %: кнопки и карта стягиваются к центру, 3D остаётся во весь экран |
| Дальность камеры на бегу | `TDKR_CAMERA_DISTANCE` | 100–200 %; 100 % — как в оригинале |
| Дальность камеры в бою | `TDKR_FIGHT_CAMERA` | 100–250 %; 100 % — как в оригинале |
| Разрешение рендера (высота) | `RENDER_HEIGHT` | 480 / 540 / 720, по умолчанию 720 |
| Лимит кадров | `FPS_LIMIT` | 30 / 60 / 90 / 120, по умолчанию 60; ему же следует ограничитель самого движка |
| Счётчик FPS | `FPS_COUNTER` | Показывает fps поверх игры |
| Буфер звука | `AUDIO_LATENCY_MS` | Размер буфера AudioTrack |
| Кадров «в полёте» | `TDKR_GL_INFLIGHT` | Сколько кадров мост может держать в очереди к GPU, по умолчанию 2 |
| Только быстрые ядра (6–7) | `CPU_PRIME` | Привязывает qemu к двум самым быстрым ядрам; по умолчанию включено |
| Подсказки производительности (ADPF) | `PERF_HINT` | Сообщает системе длительность кадров; по умолчанию выключено |
| Профилирование в логе | `QEMU_GUEST_PROF` | Диагностика |

Отступ интерфейса и дальность камеры в бою сделаны последними и в игре ещё не подтверждены. Отступ определяет
интерфейс по состоянию GL (рисование прямо на экран без теста глубины, с прозрачностью), поэтому вместе с ним
могут сжаться полноэкранные затемнения.

## Как это устроено

```
 APK-обёртка (Java, AArch64)                    дочерний процесс
 ┌──────────────────────────────┐   pipe   ┌─────────────────────────────────────────┐
 │ SettingsActivity             │ ◄──────► │ qemu-arm (наш патч, статический AArch64) │
 │ MainActivity + SurfaceView   │  команды │  └ tdkr (порт-лоадер, ARM32, glibc)      │
 │ GlBridge: исполняет GLES     │   GLES   │      ├ bionic ELF loader                 │
 │ AudioTrack, касания, геймпад │          │      ├ libKRAS.so (игра)                 │
 │ PerfHint (ADPF, телеметрия)  │          │      ├ JNI и Android API на заглушках    │
 └──────────────────────────────┘          │      └ GL-мост: сериализует вызовы GLES  │
                                           └─────────────────────────────────────────┘
```

- **Порт-лоадер** (`port/game/`, `port/portbase/`) загружает библиотеку игры, собранную под bionic, в процесс
  с glibc и подменяет libc, JNI и Java-классы игры (`port/game/jni/`).
- **GL-мост** (`port/portbase/src/gl_bridge.cpp` ↔ `port/android/src/.../GlBridge.java`): вызовы GLES пишутся
  в pipe и исполняются на стороне Java в контексте GLES 3.
- **Звук**: игра выводит звук через Java-класс `AudioTrack`; порт подставляет свой класс
  (`port/game/jni/audio_track.cpp`), тот пишет в FIFO, а обёртка играет его настоящим `AudioTrack`.
- **qemu** (`qemu/`): QEMU 11.1.2 с патчами из NOVA3 Bridge (вариант F).

История версий — в [CHANGELOG.md](CHANGELOG.md). Чем эта игра отличается от N.O.V.A. 3 и что из этого следует для порта — в [PROJECT_STATE.md](PROJECT_STATE.md).
Пошаговый разбор — что было видно, как нашли причину, что исправили — в
[docs/PORTING_GUIDE.md](docs/PORTING_GUIDE.md) ([English](docs/PORTING_GUIDE.en.md)).
Архитектура обёртки — в [port/android/README.md](port/android/README.md).

## Производительность

Используется QEMU 11.1.2 с набором правок под AArch64-хост из NOVA3 Bridge (плавающая точка и NEON на
процессоре телефона, быстрый поиск блоков при косвенных переходах; подробно в [qemu/README.md](qemu/README.md)).
Собранный вариант F взят из того проекта готовым и здесь не пересобирался.

Что дало скорость именно в этой игре (первая миссия, OnePlus 13s):

| Шаг | fps |
|---|---|
| первая сборка | 40 |
| qemu закреплён на ядрах 6–7 | 55 |
| ограничитель движка следует лимиту кадров (120) | 100–120 |

- На средних ядрах система держала главный поток игры на 1,5 ГГц с `uclamp.max` 240; на ядрах 6–7 тот же кадр
  укладывается в бюджет даже при потолке 1,2–1,4 ГГц.
- Движок сам ограничивает частоту: каждый кадр спит 1 мс и затем досыпает до 1000/60 мс, что давало потолок
  ~56 fps. Сон в 1 мс убран, а 60 заменяется лимитом из настроек (`port/game/tweaks.cpp`).

## Структура

| Папка | Что |
|---|---|
| `port/game/` | слой этой игры: точка входа, пути, Java-классы игры, ввод |
| `port/portbase/` | общая база порта: загрузчик bionic-ELF, JNI, переходники libc, GL-мост |
| `port/android/` | APK-обёртка: Java-код, манифест, сборка APK и sysroot |
| `qemu/` | патчи qemu 11.1.2, Dockerfile для кросс-сборки, тесты точности, бенчмарк |
| `tools/` | сборка и установка, запуск на ПК, логи, профилирование |

В репозитории нет файлов игры, тулчейнов и сборочных каталогов (см. `.gitignore`).

## Сборка

Сборка идёт в WSL (Ubuntu). Глобально ничего ставить не нужно, кроме уже имеющихся в системе
`gcc-arm-linux-gnueabihf`/`g++-arm-linux-gnueabihf`, `qemu-arm` и Go (для `mstrace`).

Кладутся в корень проекта и в git не входят:

- `jdk/` — JDK 17;
- `android-sdk/` — build-tools 34.0.0, platform 34, platform-tools;
- `sdl-arm/` — SDL2 под armhf;
- `qemu/out/qemu-arm-F.stripped` — собранный qemu;
- `port/android/build/sysroot/` — armhf-библиотеки (или `port/android/make_sysroot.sh`);
- `gamebin/` — ваш APK игры и архив кэша, `tdkrdata/` — распакованная игра
  (`lib/armeabi-v7a/libKRAS.so`, `files/data`, `files/textures`).

```sh
tools/build_install.sh noinstall   # порт + APK -> port/TDKR-Bridge-debug.apk
tools/build_install.sh             # то же и установка на телефон по adb
tools/run_pc.sh 600                # запуск на ПК под qemu-arm, 600 кадров, лог в logs/pc_run.log
tools/pull_log.sh                  # лог с телефона -> logs/phone.log
tools/make_release.sh              # релизный APK, архив исходников и INSTALL.txt в release/<версия>/
```

Телефон из WSL виден через виндовый `adb.exe`: обёртка `tools/adb`.

## Диагностика

- Лог игры: `tools/pull_log.sh` → `logs/phone.log`. Строка `[gl] frames` раз в несколько секунд показывает fps,
  число GL-команд, загрузку моста, текущую частоту и потолок частоты ядра, `uclamp`.
- `engine_env.txt` рядом с логом (строки `KEY=VALUE`): `TDKR_GL_PROFILE=1` — время кадра и время процессора на кадр,
  `TDKR_SLEEP_TRACE=1` — кто в движке вызывает сон.
- На ПК: `tools/run_pc.sh`, управление и скриншоты — `tools/drive.py`, замер кадра — `tools/bench_pc.py`
  (в режиме моста до миссии пока не доходит, с `--gl` доходит).
- Профиль кода игры по логу: `tools/guest_prof.py`.

## Другие игры

Тот же способ запуска — qemu-user и мост GLES на GPU телефона — для других 32-битных игр:

- [NOVA3 Bridge](https://github.com/JamesCrumble/NOVA3-Bridge) — N.O.V.A. 3 1.0.7 (Gameloft)
- [Shadowgun Bridge](https://github.com/JamesCrumble/shadowgun-bridge) — SHADOWGUN 1.7.0 (Unity 5.3)
- [Mass Effect Infiltrator Bridge](https://github.com/JamesCrumble/mass-effect-infiltrator-bridge) — Mass Effect Infiltrator 1.0.58

## Лицензия и авторство

GPL-3.0, см. [LICENSE](LICENSE). Происхождение заимствованных частей и их лицензии — в
[port/NOTICE.md](port/NOTICE.md). Патч qemu — GPL-2.0-or-later, как сам QEMU.

Порт N.O.V.A. 3 для PortMaster и его `portbase`, на которых основан проект, созданы EapRules.
Android-мост и слой этой игры написаны JamesCrumble; в работе над этим проектом участвовал Claude (Anthropic).

The Dark Knight Rises © Gameloft / Warner Bros. Проект не связан с правообладателями и не содержит файлов игры.
