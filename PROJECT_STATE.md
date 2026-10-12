# TDKR Bridge (The Dark Knight Rises -> Android APK для OnePlus 13s): состояние проекта

Цель: повторить для этой игры то, что сделано в `../nova3probe` (APK с qemu-user + мост GLES на GPU телефона).
Образец: `../nova3probe/PROJECT_STATE.md`. Архитектура обёртки: `port/android/README.md`.

Обновлено: 2026-10-12.

Рабочая папка: `/home/james/probe/the-dark-knight-rises-probe`. Всё исполняется в WSL (Ubuntu-20.04).
Телефон: OnePlus 13s (CPH2723, Snapdragon 8 Elite, только AArch64), виден через Windows `adb.exe` (обёртка `tools/adb`).

## Правила работы
- По-русски, кратко.
- Глобально ничего не ставить: тулчейны копировать из соседних проектов или ставить локально в папку проекта.
- git только локально: удалённого репозитория нет, ничего не пушить.
- Телефон: устанавливать APK и читать лог можно; **игру запускает пользователь сам** (12.10 он дважды отклонил
  команды `am force-stop` / `am start`). Данные игры на телефон уже скопированы.

## Игра
- `gamebin/The Dark Knight Rises v1.1.6.apk` (пакет `com.gameloft.android.AMAZ.GloftKRAS`, сборка для Amazon,
  в APK есть посторонние `assets/*.inject` и активити `com.inject.InjectActivity` — модифицированный APK) и
  `gamebin/The Dark Knight Rises Cache v1.1.6.7z` (1,9 ГБ в распакованном виде).
- Библиотека одна: `lib/armeabi-v7a/libKRAS.so` (12,4 МБ, SHA1 `a353e4d9218415f536bebaa873f31e65e5636cce`,
  ARMv7 + VFPv3 + NEON, SONAME `lib_Android.so`). У неё экспортированы C++-символы движка (glf, vox, glitch).
- Данные — плоские файлы: `files/data/*.gla`, `files/textures/*.gla`. OBB нет.

### Отличия от N.O.V.A. 3, из-за которых слой игры переписан
| что | N.O.V.A. 3 | TDKR |
|---|---|---|
| JNI-класс с точками входа | `com/gameloft/android/ANMP/GloftN3HM/GL2JNILib` | `com/gameloft/glf/GL2JNILib` |
| обратные вызовы в Java | один класс `GL2JNIActivity` | `com/gameloft/glf/GL2JNIActivity` и `GL2JNILib` (оба несут одну таблицу методов), `GLUtils/SUtils`, `GLUtils/Device`, `installer/GameInstaller`, `SplashScreenActivity` |
| `setPaths` | 4 строки (obb, data, home, temp) | 3 строки (resource, home, temp) |
| данные | два OBB (zip), ленивая распаковка | плоские файлы, каталог ресурсов = `<game>/files` |
| звук | OpenSL ES | Java `android/media/AudioTrack` через JNI (`write([BII)I` из потока vox) |
| DRM | четыре `lockPointer` | нет |
| GL | GLES2 | GLES2, дополнительно импортирует `glUniformMatrix2fv`, `glUniformMatrix3fv` (добавлены в мост) |
| ввод | `nativePowerStatus`, левый и правый стики | `nativeSetPowerAConnected`, только правый стик `(FF)V` |

Коды касаний те же (из `GL2JNIView.onTouchEvent`): 1 — вниз, 2 — движение, 0 — вверх, плюс id пальца.
Порядок запуска: `JNI_OnLoad` -> `init` (движок сам зовёт `createView`) -> `initGL` -> `InitViewSettings`
(зовёт `setupPaths` -> `setPaths` и `setViewSettings`) -> `resize` -> `stateChanged(true)` -> `step` в цикле.

## Что есть в папке
| путь | что |
|---|---|
| `port/game/` | слой игры: `main.cpp`, `io_paths.cpp`, `input_bridge.cpp`, `tweaks.cpp`, `jni/` (классы игры, `AudioTrack`) |
| `port/portbase/` | база порта из nova3probe (идентификаторы `nova3` переименованы в `tdkr`) |
| `port/android/` | APK-обёртка (пакет `com.jamescrumble.tdkr`), `build_apk.sh` |
| `qemu/` | патчи и сборочные файлы qemu из nova3probe; бинарник `qemu/out/qemu-arm-F.stripped` скопирован, здесь не пересобирался |
| `jdk/ android-sdk/ sdl-arm/` | локальные тулчейны, скопированы из nova3probe (в git не входят) |
| `port/android/build/sysroot/` | armhf sysroot, скопирован из nova3probe |
| `tdkrdata/` | распакованная игра: `lib/armeabi-v7a/libKRAS.so`, `files/` (кэш) |
| `extracted/` | разбор APK: `classes.txt` (dexdump), `strings.txt`, `imports.txt` |
| `tools/` | `build_install.sh`, `run_pc.sh`, `push_data.sh`, `pull_log.sh`, `adb` и скрипты диагностики из nova3probe |

## Сделано (2026-10-12)
- Порт собирается (`./build.sh` -> `port/build/tdkr`), APK собирается (`tools/build_install.sh noinstall` ->
  `port/TDKR-Bridge-debug.apk`, 57 МБ, debug-ключ `port/android/build/debug.keystore`). Иконка берётся из APK игры.
- На ПК (`tools/run_pc.sh`, qemu-arm + llvmpipe): 0 неразрешённых импортов, игра доходит до главного меню,
  картинка правильная (скриншоты `ctl/screenshots/`).
- На телефоне: установлено, данные игры лежат в `Android/data/com.jamescrumble.tdkr/files/`. Запуск пользователем:
  главное меню, ~55 fps, касания доходят, фаза PLAYING, в логе `AudioTrack: direct output` и запись блоков.
- Исправлено по ходу: (1) папки, созданные `adb push`, принадлежат `shell` и закрыты приложению —
  `chmod 2777` на папки, приложение теперь пишет об этом на экране; (2) в мосте не было
  `glUniformMatrix2fv/3fv` — добавлены (виды 9 и 10 в `GB_OP_UNIFORM_V`, обе стороны).

## Не проверено
- Звук на слух, игра в миссии, долгая игра, пауза/возврат в приложение, кнопка «Назад».
- Геймпад: коды кнопок в `input_bridge.cpp` остались от N.O.V.A. 3; правый стик зовётся с двумя числами.
- Настройки из меню (лимит кадров, кадров «в полёте», ядра 6–7, ADPF) на этой игре.
- ETC1-текстуры: мост печатает «no decoder ... forwarding to the driver» и отдаёт их Adreno как есть
  (расширение ETC1 у драйвера есть); на ПК под llvmpipe такие текстуры могут быть пустыми.
- Сборка qemu из исходников в этой папке (исходников `qemu-src/` здесь нет).

## Убрано из слоя N.O.V.A. 3
`obb_cache.cpp`, `patch.cpp` (DRM), `xml_hook.cpp` и хуки по смещениям в `tweaks.cpp` (FOV, физика в отдельном
потоке), `SaveMigration.java`, настройки качества в меню, упаковка для PortMaster и `harness/`. В `tweaks.cpp`
остались замена kuser cmpxchg (43 места в этой библиотеке) и uclamp.

## Дальше
- Проверить миссию, звук, управление в бою; снять лог (`tools/pull_log.sh`) и смотреть `[gl] frames`, `slow frame`.
- Оптимизации по образцу nova3: профиль (`QEMU_GUEST_PROF`), профили качества движка (`data/options/GPU_*.xml`,
  выбор по GL_RENDERER; `gpu_name.txt` подменяет имя), закрепление на ядрах 6–7.
- Раскладка геймпада под эту игру.
