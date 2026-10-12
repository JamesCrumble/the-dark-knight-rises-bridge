# TDKR Bridge

**English:** run **The Dark Knight Rises 1.1.6** (Gameloft, 32-bit ARMv7, Amazon build `GloftKRAS`) on
64-bit-only Android phones that have no AArch32 support. The game runs under a patched `qemu-user`, and its
OpenGL ES calls are forwarded to the phone's real GPU. First build; started on a OnePlus 13s (Snapdragon 8 Elite).
No game files are included: you need your own copy. The rest of this page is in Russian.

Запуск 32-битной **The Dark Knight Rises** (Gameloft, v1.1.6, только `armeabi-v7a`) на 64-битных
Android-телефонах, в которых нет поддержки AArch32.

Игра исполняется в пропатченном `qemu-arm` (linux-user), а её вызовы OpenGL ES пробрасываются на настоящий
GPU телефона. Способ тот же, что в NOVA3 Bridge, Shadowgun Bridge и Mass Effect Infiltrator Bridge; за основу
взят NOVA3 Bridge (тот же движок Gameloft).

**Игра не распространяется.** Нужны ваши собственные APK версии 1.1.6 и кэш к ней.

## Состояние

Первая сборка (0.1).

- На ПК (qemu-arm + программный GL): игра загружается до главного меню, картинка правильная.
- На OnePlus 13s: игра запускается, главное меню идёт ~55 fps, касания доходят до игры.
- Не проверено: прохождение миссий, звук на слух (по логу игра открывает звуковой выход и пишет в него),
  геймпад (раскладка кнопок осталась от N.O.V.A. 3), долгая игра.

## Установка

1. Установите `TDKR-Bridge-debug.apk` (пакет `com.jamescrumble.tdkr`).
2. Положите в `/sdcard/Android/data/com.jamescrumble.tdkr/files/`:
   - оригинальный APK игры 1.1.6;
   - папку `com.gameloft.android.AMAZ.GloftKRAS` из распакованного архива кэша (внутри `files/data`,
     `files/textures`).
3. Если копировали по adb, откройте папки приложению (adb создаёт их от имени `shell`):
   `adb shell "find /sdcard/Android/data/com.jamescrumble.tdkr/files/com.gameloft.android.AMAZ.GloftKRAS -type d -exec chmod 2777 {} +"`
4. Откройте приложение и нажмите «Играть».

Из папки проекта всё это делает `tools/push_data.sh`. Если чего-то не хватает, причина пишется на экране и в
`tdkr.log` в папке с файлами игры.

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

Чем эта игра отличается от N.O.V.A. 3 и что из этого следует для порта — в [PROJECT_STATE.md](PROJECT_STATE.md).
Архитектура обёртки — в [port/android/README.md](port/android/README.md).

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
```

Телефон из WSL виден через виндовый `adb.exe`: обёртка `tools/adb`.

## Лицензия и авторство

GPL-3.0, см. [LICENSE](LICENSE). Происхождение заимствованных частей и их лицензии — в
[port/NOTICE.md](port/NOTICE.md). Патч qemu — GPL-2.0-or-later, как сам QEMU.

Порт N.O.V.A. 3 для PortMaster и его `portbase`, на которых основан проект, созданы EapRules.
Android-мост и слой этой игры написаны JamesCrumble.

The Dark Knight Rises © Gameloft / Warner Bros. Проект не связан с правообладателями и не содержит файлов игры.
