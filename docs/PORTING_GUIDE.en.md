# How TDKR Bridge was built: a step-by-step account

Русская версия: [PORTING_GUIDE.md](PORTING_GUIDE.md).

The Dark Knight Rises 1.1.6 is a Gameloft game on the same engine as N.O.V.A. 3, in a newer version of it.
It is the shortest port of them all: the frame was taken whole from NOVA3 Bridge and only the game layer
was rewritten. So this guide answers two questions: what has to be checked when "the engine is the same",
and how to give a game settings it does not have — frame rate and camera distance.

Every step has the same shape: what was seen, how the cause was found, what was changed and where. The
current state is in [PROJECT_STATE.md](../PROJECT_STATE.md) (Russian), the summary in
[README.md](../README.md).

Status: menu, first mission, combat; 100–120 fps in the first mission with the limit at 120 on a OnePlus 13s.

## Starting point

- The APK is an Amazon build (`com.gameloft.android.AMAZ.GloftKRAS`) and a modified one: it carries foreign
  `assets/*.inject` files and a `com.inject.InjectActivity`. That does not matter to the port: only the
  library is needed from the APK.
- One library: `libKRAS.so`, 12.4 MB, ARMv7 + VFPv3 + NEON. The engine's symbols (`glf`, `vox`, `glitch`)
  are exported, so functions are found by name.
- The data is flat files (`files/data/*.gla`, `files/textures/*.gla`); there is no OBB.

## Part 1. What to check when the engine is "the same"

The frame is copied and the identifiers renamed. Then everything the game uses to talk to the outside
world is checked against a list. Between TDKR and N.O.V.A. 3, eight items out of eight differed:

| What | N.O.V.A. 3 | TDKR | How to find out |
|---|---|---|---|
| class with the entry points | `<package>/GL2JNILib` | `com/gameloft/glf/GL2JNILib` | the library's `Java_*` exports |
| callback classes | one `GL2JNIActivity` | `GL2JNIActivity` and `GL2JNILib`, plus `SUtils`, `Device`, `GameInstaller`, `SplashScreenActivity` | classpath-shaped strings in the library |
| `setPaths` | 4 strings | 3 strings (resource, home, temp) | the signature in the dex |
| data | two OBBs, unpacked lazily | flat files | the contents of the cache archive |
| audio | OpenSL ES | a Java `AudioTrack` through JNI | `readelf -d`: no `libOpenSLES` |
| DRM | four `lockPointer`s | none | the symbol table |
| GL | GLES2 | GLES2 plus `glUniformMatrix2fv/3fv` | the `gl*` imports |
| input | both sticks | right stick only, `(FF)V` | exports and the dex |

Removed from the N.O.V.A. 3 layer as not needed: the OBB cache, the DRM patch, the XML hook, the field of
view and physics on its own thread (they are tied to offsets in another library), and save migration.

### 1.1. Two classes with one method table

- **Observation:** on Android the methods are split between `GL2JNIActivity` and `GL2JNILib`. Which class
  the engine uses for each lookup was not traced.
- **Solution:** both fake classes carry the whole table —
  [gl2jni_activity.cpp](../port/game/jni/gl2jni_activity.cpp).
- **Why:** it is cheaper than tracing and cannot be wrong.

### 1.2. Start-up order

`JNI_OnLoad` → `init` → `initGL` → `InitViewSettings` → `resize` → `stateChanged(true)` → `step` in a loop.

Two places where the obvious order is wrong ([main.cpp](../port/game/main.cpp)):

- `setPaths` must not be called by the port. From inside `init()` the engine calls `setupPaths` in Java and
  the fake class answers it. Calling it early faults: the filesystem object does not exist yet.
- Without `stateChanged(true)` after the first `resize` the engine stays in its "no surface" state.

### 1.3. Touch codes

Taken from `GL2JNIView.onTouchEvent` in the dex: 1 is a finger down, 2 a move, 0 a finger up, plus the
finger's id. They matched N.O.V.A. 3, but that was checked, not assumed.

## Part 2. Audio through Java

- **Seen:** the library is not linked against `libOpenSLES`.
- **How it was found:** the library's strings hold, side by side, `android/media/AudioTrack`, the
  constructor `(IIIIII)V`, `getMinBufferSize`, `play`, `write([BII)I`. The `vox` audio driver creates a
  Java object and writes finished PCM into it from its own thread.
- **The key detail:** `AudioTrack.write` blocks while the device buffer is full. That block is the only
  thing pacing the mixer thread. Whatever replaces it has to block the same way.
- **Fix:** [audio_track.cpp](../port/game/jni/audio_track.cpp) — a fake `AudioTrack` class. In the APK it
  writes the PCM into a small pipe that a real `AudioTrack` reads; the pipe filling up is the pacing.
- **Check in the log:** the line `AudioTrack: direct output` and blocks being written. Not checked by ear.

## Part 3. First run on the phone

### 3.1. The app cannot see its own files

- **Seen:** the data has been copied, the game does not find it.
- **Cause:** folders made by `adb push` belong to the `shell` user and are closed to the app.
- **Fix:** `chmod 2777` on the folders ([push_data.sh](../tools/push_data.sh)). The wrapper now tells
  "no files" from "files present but access closed" and says so on screen with the command to run.

### 3.2. Two GL functions

`glUniformMatrix2fv` and `glUniformMatrix3fv` were added to the bridge as two new kinds of the `UNIFORM_V`
op — on both sides ([gl_bridge.cpp](../port/portbase/src/gl_bridge.cpp)).

### 3.3. ETC1 textures

The bridge prints "no decoder … forwarding to the driver" and hands them to the driver as they are: Adreno
has the ETC1 extension. On the PC under software rendering such textures may come out empty.

## Part 4. Speed: 40 → 120 fps

Two steps, both found by measuring rather than guessing.

### 4.1. Cores: 40 → 55

- **Seen:** 40 fps in the mission.
- **How it was found:** the `[gl] frames` line shows the core, its frequency and `uclamp`. On the middle
  cores the system held the main thread at 1.5 GHz with `uclamp.max` 240.
- **Fix:** `CPU_PRIME` is on by default — qemu is pinned to cores 6–7.
- **Result:** 55 fps. Exactly 55 — and that turned out to be the clue to the next step.

### 4.2. The engine's own frame limiter: 55 → 100–120

- **Seen:** the frame rate sits at ~55–56 whatever the wrapper's limit, and the CPU is not busy.
- **How it was found:** a trace was added, `TDKR_SLEEP_TRACE=1` — who calls `glf::Thread::Sleep`, for how
  long and from which thread. A three-instruction function can be replaced outright. The trace showed two
  calls in the application's per-frame method (`+0x40d6e0`):

```
40d834  mov r0, #1
40d838  bl  glf::Thread::Sleep     ; 1 ms every frame, whatever the frame cost
...     remaining = frame_start + budget − time_after_update
40d874  b   glf::Thread::Sleep     ; the remainder, when positive
```

- **Cause:** the budget is 1000/60 ms, kept next to the rate 60 in the application object. A menu frame
  takes 1 + 16 + its own half a millisecond, that is 56 frames a second. The wrapper's limit has no say.
- **Fix** ([tweaks.cpp](../port/game/tweaks.cpp)):
  - the unconditional 1 ms sleep is dropped (under qemu every guest thread is a host thread; there is
    nothing to yield to);
  - wherever the game set itself 60, `TDKR_TARGET_FPS` is put in, and the wrapper passes its frame limit.
- **How it is made safe:** the sleep is found by offset and the fields by the object's layout, so both are
  checked before being touched: the instruction pair, and "the rate is 60 and the budget is 1000/60". In
  another version of the library the check fails and the port touches nothing. The value is applied once
  a frame rather than by patching the setter — that catches the constructor's 60 too. The other value the
  game uses (1000) is left alone.
- **Result:** menu 120, first mission 100–120.

### 4.3. A note on frequency

On cores 6–7 the same frame fits its budget even with the frequency ceiling at 1.2–1.4 GHz.

## Part 5. Settings the game does not have

### 5.1. Camera distance while running

- **How it was found:** the camera controller keeps its offset from the hero as a vector with a multiplier
  next to it. The script function `SetCameraOffsetMultiplier` sets the multiplier. Three places in the
  code compute "vector × multiplier", each starting with the same load.
- **The first attempt failed:** writing our value into the field. The engine rewrites it itself — six
  times in the first seconds of a mission.
- **Fix:** the load of the multiplier at the three places is replaced with a branch to a small stub: load,
  multiply by the factor, branch back. The register used for the factor is one the next instruction
  overwrites anyway.
- **Confirmed by the user:** it works while running.

### 5.2. Camera distance in a fight

The camera has a mode; mode 5 is the fight camera, `CameraKombat`. It works in spherical terms and eases
the current distance towards the wanted one. The load of the wanted distance is replaced with a stub that
multiplies it. **Not confirmed by the user.**

### 5.3. Interface inset from the edges

- **Goal:** pull the buttons and the map towards the centre (rounded screen corners) while the 3D picture
  keeps the whole screen.
- **Difficulty:** nothing in the GL stream names the interface as such.
- **Solution:** the interface is recognised by state — a draw into the window, into the full-screen
  viewport, with the depth test off and blending on. Such a draw gets a shrunk viewport
  ([gl_bridge.cpp](../port/portbase/src/gl_bridge.cpp)). Touches are mapped back in
  [input_bridge.cpp](../port/game/input_bridge.cpp).
- **Side effect:** full-screen fades may shrink along with the interface.
- **What was rejected:** insetting the whole picture with black bars — the user declined it.
- **Not confirmed** by the user in the game.

## Part 6. Checking on the PC

`tools/run_pc.sh` runs the port under the host's qemu-arm with software rendering; `tools/drive.py` sends
commands and takes screenshots. On the PC the game reaches the main menu with a correct picture. The frame
benchmark (`tools/bench_pc.py`) does not reach the mission in bridge mode: driving blind, it misses the
Continue button; with real GL (`--gl`) it gets there.

The PC run needed a stencil buffer: without it the minimap's clipping is lost and the map plane covers a
third of the screen.

## Rules that proved useful

1. **"The same engine" does not mean "the same game layer".** Classes, signatures, data, audio, DRM, GL
   imports and input are checked against a list.
2. **A round frame rate is a clue.** 55–56 is 1000 / (1 + 16.7 + ε).
3. **Trace the sleep instead of guessing.** Who, for how long, from which thread.
4. **Guard a patch made by offset** with a check of the original instructions and values.
5. **When the engine rewrites a field itself, patch where it is read**, not the value.

## What is left

- Audio by ear, long play, pause and return to the app, the Back button.
- Gamepad: the button codes are still N.O.V.A. 3's.
- The fight camera and the interface inset are not confirmed in the game.
- The engine's quality profiles (`data/options/GPU_*.xml`) have not been looked at.
