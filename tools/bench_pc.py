#!/usr/bin/env python3
"""Measures what one frame of gameplay costs the game's main thread, on this PC, without a phone.

  bench_pc.py [--frames N] [--name NAME] [--gl] [--keep] [KEY=VALUE ...]

The game runs under the system qemu-arm with the GL bridge talking to the Python stand-in host
(port/android/tools/glbridge_host_test.py), so nothing is rendered and the frame time is the game's own
code plus the bridge. The script starts from the save in bench/saves_mission1 (the start of mission 1),
taps Continue, skips the cinematic and then averages the port's "glprof" lines over N frames of the
opening scene of Gotham.

  --gl      render with Mesa llvmpipe instead (slow; screenshots work: tools/drive.py shot NAME)
  --keep    leave the game running at the end (stop it with: tools/drive.py quit)
  KEY=VALUE extra environment for the port, e.g. TDKR_OPTIONS_FILE=/path/override.xml

The numbers are for comparing one run with another on the same PC. The host qemu here is stock and x86,
so they say nothing about frames per second on a phone.
"""
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PORT = os.path.join(ROOT, 'port')
GAME = os.path.join(ROOT, 'tdkrdata')
SAVES = os.path.join(ROOT, 'bench', 'saves_mission1')
CTL = os.path.join(ROOT, 'ctl')
os.environ['TDKR_CONTROL_DIR'] = CTL
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import drive  # noqa: E402


def main():
    args = sys.argv[1:]
    frames, name, use_gl, keep, extra = 600, 'bench', False, False, {}
    while args:
        a = args.pop(0)
        if a == '--frames':
            frames = int(args.pop(0))
        elif a == '--name':
            name = args.pop(0)
        elif a == '--gl':
            use_gl = True
        elif a == '--keep':
            keep = True
        elif '=' in a:
            k, v = a.split('=', 1)
            extra[k] = v
        else:
            sys.exit(__doc__)

    if not os.path.isdir(SAVES):
        sys.exit('no save to start from: ' + SAVES)
    shutil.rmtree(os.path.join(GAME, 'saves'), ignore_errors=True)
    shutil.copytree(SAVES, os.path.join(GAME, 'saves'))

    os.makedirs(CTL, exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'logs'), exist_ok=True)
    open(os.path.join(CTL, 'commands'), 'w').close()
    for f in ('status.json',):
        try:
            os.remove(os.path.join(CTL, f))
        except OSError:
            pass
    log_path = os.path.join(ROOT, 'logs', 'bench_%s.log' % name)
    host_log = os.path.join(ROOT, 'logs', 'bench_%s_host.log' % name)

    # 16:9, so the positions found at 480x270 scale by one factor.
    w, h = (480, 270) if use_gl else (1280, 720)
    sx, sy = w / 480.0, h / 270.0
    env = dict(os.environ)
    env.update({
        'LOADER_TRACE': '1', 'SDL_VIDEODRIVER': 'offscreen', 'SDL_AUDIODRIVER': 'dummy',
        'TDKR_WIDTH': str(w), 'TDKR_HEIGHT': str(h), 'TDKR_TOUCH': '1', 'TDKR_CURSOR': '0',
        'TDKR_FB_PROBE_INTERVAL': '0', 'TDKR_GL_PROFILE': '1',
    })
    host = None
    if use_gl:
        env.update({'LIBGL_ALWAYS_SOFTWARE': '1', 'EGL_PLATFORM': 'surfaceless'})
    else:
        cmd_fifo, rep_fifo = os.path.join(CTL, 'gl_cmd.pipe'), os.path.join(CTL, 'gl_reply.pipe')
        for p in (cmd_fifo, rep_fifo):
            if os.path.exists(p):
                os.remove(p)
            os.mkfifo(p)
        host = subprocess.Popen([sys.executable, '-I', os.path.join(PORT, 'android/tools/glbridge_host_test.py'),
                                 cmd_fifo, rep_fifo, '100000'], stdout=open(host_log, 'w'), stderr=subprocess.STDOUT)
        env.update({'TDKR_GL_BRIDGE': '1', 'TDKR_GL_CMD': cmd_fifo, 'TDKR_GL_REPLY': rep_fifo,
                    'TDKR_GL_RENDERER': 'Adreno (TM) 830'})
    env.update(extra)

    game = subprocess.Popen(['qemu-arm', '-L', 'android/build/sysroot', 'build/tdkr', GAME], cwd=PORT, env=env,
                            stdout=open(log_path, 'w'), stderr=subprocess.STDOUT)

    def log():
        return open(log_path, errors='replace').read()

    def wait_log(pattern, timeout, after=0):
        end = time.time() + timeout
        while time.time() < end:
            if game.poll() is not None:
                sys.exit('the game exited (code %s) while waiting for %r; see %s' % (game.returncode, pattern, log_path))
            m = re.search(pattern, log()[after:])
            if m:
                return after + m.end()
            time.sleep(0.5)
        sys.exit('timeout waiting for %r; see %s' % (pattern, log_path))

    def tap(x, y):
        drive.run(['tap', str(int(x * sx)), str(int(y * sy))])

    def step(text):
        print('[bench] %s (frame %d)' % (text, drive.frame()), flush=True)

    try:
        pos = wait_log(r'slow frame \d+: .*last g\.dat', 600)
        step('menu assets loaded')
        drive.wait_frame(drive.frame() + 300)
        tap(240, 135)                      # "Touch the screen to continue"
        drive.wait_frame(drive.frame() + 250)
        step('tapping Continue')
        tap(82, 78)
        pos = wait_log(r'last cinematics\.gla', 900, pos)
        step('mission loaded')
        drive.wait_frame(drive.frame() + 12)
        tap(456, 249)                      # skip the cinematic
        drive.wait_frame(drive.frame() + (30 if use_gl else 240))
        step('measuring %d frames' % frames)
        mark = len(log())
        drive.wait_frame(drive.frame() + frames, 3600)
        text = log()[mark:]
    finally:
        if not keep:
            drive.send('quit')
            try:
                game.wait(20)
            except subprocess.TimeoutExpired:
                game.kill()
            if host:
                host.kill()

    rows = re.findall(r'glprof: step ([\d.]+) ms, cpu ([\d.]+) ms \(bridge ([\d.]+) ms, (\d+) calls\), swap wait ([\d.]+)', text)
    if not rows:
        sys.exit('no glprof lines in the measured part; see ' + log_path)
    n = len(rows)
    avg = [sum(float(r[i]) for r in rows) / n for i in range(5)]
    print('[bench] %s: %d x 120 frames: step %.2f ms, cpu %.2f ms, bridge %.2f ms, %.0f GL calls, swap wait %.2f ms'
          % (name, n, avg[0], avg[1], avg[2], avg[3], avg[4]))
    print('[bench] per interval cpu ms: ' + ' '.join(r[1] for r in rows))


if __name__ == '__main__':
    main()
