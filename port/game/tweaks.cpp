/*
 * Engine tweaks that do not depend on offsets inside the library.
 *
 *   <PREFIX>_FAST_CAS=0   turn off the atomics fix below (on by default). A library built for ARMv5
 *                         calls the kernel's cmpxchg helper at 0xffff0fc0 for its __sync_* functions,
 *                         which qemu-user emulates by trapping out of translated code on every call.
 *                         Each literal holding that address is pointed at a small ldrex/strex routine.
 *   <PREFIX>_TARGET_FPS, <PREFIX>_FRAME_YIELD   the engine's frame limiter, see install_frame_limit.
 *   <PREFIX>_SLEEP_TRACE=1   who calls glf::Thread::Sleep, see sleep_trace_hook.
 *   <PREFIX>_UCLAMP=0     leave the scheduler's clamp on the main thread alone (see tdkr_sched_tick).
 *   <PREFIX>_UCLAMP_MIN=0..1024 also ask for at least this much (0 by default).
 */
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "so_util.h"
#include "trace.h"
#include "port_env.h"

#include "tdkr.h"

static void install_fast_cas(so_module *mod)
{
    /* kuser cmpxchg: r0 = old, r1 = new, r2 = ptr; returns r0 = 0 when it stored. */
    static const uint32_t code[] = {
        0xf57ff05b, /* dmb ish            */
        0xe1923f9f, /* 1: ldrex r3,[r2]   */
        0xe0533000, /* subs r3,r3,r0      */
        0x01823f91, /* strexeq r3,r1,[r2] */
        0x03330001, /* teqeq r3,#1        */
        0x0afffffa, /* beq 1b             */
        0xf57ff05b, /* dmb ish            */
        0xe1a00003, /* mov r0,r3          */
        0xe12fff1e, /* bx lr              */
    };
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        trace("tweaks: fast cas: mmap failed");
        return;
    }
    memcpy(page, code, sizeof(code));
    uint32_t *w = (uint32_t *)mod->text_base;
    size_t n = mod->text_size / 4, hits = 0;
    for (size_t i = 0; i < n; i++) {
        if (w[i] == 0xffff0fc0u) {
            w[i] = (uint32_t)(uintptr_t)page;
            hits++;
        }
    }
    trace("tweaks: kuser cmpxchg literal replaced at %zu places (routine %p)", hits, page);
}

/*
 * <PREFIX>_SLEEP_TRACE=1: who calls glf::Thread::Sleep, for how long and from which thread.
 * The function is three instructions (ms * 1000, tail call to usleep), so it is replaced outright.
 * One line per distinct call site, then a count line every 600 calls.
 */
static so_module *g_trace_mod;

static void sleep_trace_hook(unsigned ms)
{
    static struct { uintptr_t lr; unsigned calls; unsigned long total_ms; } sites[32];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static unsigned long calls;
    uintptr_t lr = (uintptr_t)__builtin_return_address(0) - g_trace_mod->text_base;

    pthread_mutex_lock(&lock);
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        if (sites[i].lr == lr || !sites[i].lr) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        if (!sites[slot].lr) {
            sites[slot].lr = lr;
            trace("sleep trace: new call site +0x%lx: %u ms, thread %ld%s", (unsigned long)lr, ms,
                  (long)syscall(SYS_gettid), syscall(SYS_gettid) == getpid() ? " (main)" : "");
        }
        sites[slot].calls++;
        sites[slot].total_ms += ms;
    }
    if (++calls % 600 == 0) {
        for (int i = 0; i < 32 && sites[i].lr; i++) {
            if (sites[i].calls)
                trace("sleep trace: +0x%lx: %u calls, %lu ms in total", (unsigned long)sites[i].lr,
                      sites[i].calls, sites[i].total_ms);
            sites[i].calls = 0;
            sites[i].total_ms = 0;
        }
    }
    pthread_mutex_unlock(&lock);
    usleep(ms * 1000u);
}

/*
 * The engine's own frame limiter.
 *
 * The application's per-frame method (+0x40d6e0 in the 1.1.6 library) ends like this:
 *
 *     40d834  mov r0, #1
 *     40d838  bl  glf::Thread::Sleep        ; 1 ms, every frame, whatever the frame cost
 *     ...     remaining = frame_start + budget - time_after_update
 *     40d874  b   glf::Thread::Sleep        ; (int)remaining ms, when positive
 *
 * The budget is a float in ms at this+0x11f10, kept next to the frame rate it was derived from (an int
 * at this+0x11f0c): 60 from the constructor and from six call sites of a small setter, 1000 from one
 * more. So an idle menu frame takes 1 + 16 + its own half a millisecond - 56 frames a second - and
 * nothing the wrapper's frame limit says changes that.
 *
 *   <PREFIX>_TARGET_FPS=N    wherever the game has set itself 60, it gets N instead (the wrapper passes
 *                            its frame limit). Applied once a frame from tdkr_sched_tick rather than by
 *                            patching the setter, so the constructor's own 60 is caught as well; the
 *                            other value the game uses (1000) is left alone.
 *   <PREFIX>_FRAME_YIELD=1   keep the unconditional 1 ms sleep (dropped by default: under qemu-user
 *                            every guest thread is a host thread and needs no yielding to).
 *
 * The sleep is found by offset and the two fields by layout, so each is checked before it is touched:
 * the instruction pair for one, "the rate is 60 and the budget is 1000/60" for the other.
 */
static int   g_target_fps;
static void *(*g_app_instance)(void);

static void frame_limit_tick(void)
{
    if (g_target_fps <= 0 || !g_app_instance)
        return;
    char *app = (char *)g_app_instance();
    if (!app)
        return;
    int   *rate   = (int *)(app + 0x11f0c);
    float *budget = (float *)(app + 0x11f10);
    if (*rate != 60 || fabsf(*budget - 1000.0f / 60.0f) > 0.01f)
        return;
    *rate   = g_target_fps;
    *budget = 1000.0f / (float)g_target_fps;
    static int announced;
    if (announced < 4) {
        announced++;
        trace("tweaks: frame limiter: the game's 60 fps target set to %d", g_target_fps);
    }
}

static void install_frame_limit(so_module *mod)
{
    long fps = port_getenv_long("TARGET_FPS", 0);
    if (fps >= 20 && fps <= 240 && fps != 60) {
        g_app_instance = (void *(*)(void))so_symbol(mod, "_ZN3glf3App11GetInstanceEv");
        if (g_app_instance)
            g_target_fps = (int)fps;
        else
            trace("tweaks: frame limiter: no glf::App::GetInstance in this library - target left at 60");
    }

    if (!port_getenv_long("FRAME_YIELD", 0)) {
        uint32_t *yield = (uint32_t *)(mod->text_base + 0x40d834);
        /* mov r0, #1 ; bl <...> */
        if (yield[0] == 0xe3a00001u && (yield[1] & 0xff000000u) == 0xeb000000u) {
            yield[1] = 0xe1a00000u;     /* nop */
            __builtin___clear_cache((char *)&yield[1], (char *)&yield[2]);
            trace("tweaks: frame limiter: the 1 ms sleep of every frame is dropped");
        } else {
            trace("tweaks: frame limiter: no per-frame sleep at +0x40d834, not the 1.1.6 library - left alone");
        }
    }
}

/*
 * <PREFIX>_CAMERA_DISTANCE=130: the camera stands 1.3 times farther from the hero.
 *
 * The camera controller keeps its offset from the hero as a vector (floats at +896, +900, +904) and a
 * multiplier next to it (+920; the script function SetCameraOffsetMultiplier at +0x185aac stores its
 * argument there). Three places compute vector * multiplier, each starting with the same load:
 *
 *     200144  vldr s15, [r4, #920]      208f34  vldr s15, [r4, #920]      200e3c  vldr s15, [r1, #920]
 *
 * and each loads s14 next, so s14 is free at that point. The load is replaced by a branch to a small
 * stub (load, times the factor, branch back). Writing the factor into the field instead does not hold:
 * the engine rewrites the field itself, six times in the first seconds of a mission.
 */
extern uintptr_t so_alloc_arena(so_module *so, uintptr_t range, uintptr_t dst, size_t sz);

static uint32_t arm_branch(uintptr_t from, uintptr_t to)
{
    return 0xea000000u | ((uint32_t)((to - (from + 8)) >> 2) & 0x00ffffffu);
}

static void install_camera_distance(so_module *mod, float factor)
{
    static const struct { uint32_t offset, load; } kSites[] = {
        { 0x200144, 0xedd47ae6u }, { 0x208f34, 0xedd47ae6u }, { 0x200e3c, 0xedd17ae6u },
    };
    int done = 0;
    for (unsigned i = 0; i < sizeof(kSites) / sizeof(kSites[0]); i++) {
        uint32_t *site = (uint32_t *)(mod->text_base + kSites[i].offset);
        if (*site != kSites[i].load)
            continue;
        uint32_t *stub = (uint32_t *)so_alloc_arena(mod, 0x01000000, (uintptr_t)site, 5 * sizeof(uint32_t));
        if (!stub)
            continue;
        stub[0] = kSites[i].load;       /* vldr s15, [rN, #920] */
        stub[1] = 0xed9f7a01u;          /* vldr s14, [pc, #4]   */
        stub[2] = 0xee677a87u;          /* vmul.f32 s15, s15, s14 */
        stub[3] = arm_branch((uintptr_t)&stub[3], (uintptr_t)(site + 1));
        memcpy(&stub[4], &factor, sizeof(factor));
        __builtin___clear_cache((char *)stub, (char *)(stub + 5));
        *site = arm_branch((uintptr_t)site, (uintptr_t)stub);
        __builtin___clear_cache((char *)site, (char *)(site + 1));
        done++;
    }
    /*
     * The same two functions have a second branch, taken in the fight camera, that skips the multiplier:
     * x and z come straight from the vector and the height from two other fields (already in s15 / s19).
     *
     *     200034  vldr s13, [r4, #896] ; vstr [sp, #76] ; vldr s13, [r4, #904] ; vstr [sp, #84] ; vstr s15, [sp, #80]
     *     208dd8  vldr s18, [r4, #896] ; vldr s16, [r4, #904]
     *
     * Each run is replaced by a stub doing the same with all three components times the factor.
     */
    static const uint32_t kStubA[] = {
        0xed9f7a09u, 0xedd46ae0u, 0xee666a87u, 0xedcd6a13u, 0xedd46ae2u, 0xee666a87u, 0xedcd6a15u,
        0xee677a87u, 0xedcd7a14u, 0xed947ad0u, 0 /* b 200048 */, 0 /* factor */,
    };
    static const uint32_t kStubB[] = {
        0xed2d7b02u, 0xed9f7a06u, 0xee699a87u, 0xed949ae0u, 0xee299a07u, 0xed948ae2u, 0xee288a07u,
        0xecbd7b02u, 0 /* b 208de0 */, 0 /* factor */,
    };
    static const struct { uint32_t offset, first, resume; const uint32_t *code; unsigned words; } kRaw[] = {
        { 0x200034, 0xedd46ae0u, 0x200048, kStubA, 12 },
        { 0x208dd8, 0xed949ae0u, 0x208de0, kStubB, 10 },
    };
    int raw = 0;
    for (unsigned i = 0; i < 2; i++) {
        uint32_t *site = (uint32_t *)(mod->text_base + kRaw[i].offset);
        if (*site != kRaw[i].first)
            continue;
        unsigned n = kRaw[i].words;
        uint32_t *stub = (uint32_t *)so_alloc_arena(mod, 0x01000000, (uintptr_t)site, n * sizeof(uint32_t));
        if (!stub)
            continue;
        memcpy(stub, kRaw[i].code, n * sizeof(uint32_t));
        stub[n - 2] = arm_branch((uintptr_t)&stub[n - 2], mod->text_base + kRaw[i].resume);
        memcpy(&stub[n - 1], &factor, sizeof(factor));
        __builtin___clear_cache((char *)stub, (char *)(stub + n));
        *site = arm_branch((uintptr_t)site, (uintptr_t)stub);
        __builtin___clear_cache((char *)site, (char *)(site + 1));
        raw++;
    }
    trace("tweaks: camera: distance x%.2f at %d of 3 places, fight camera at %d of 2", factor, done, raw);
}

/*
 * <PREFIX>_FIGHT_CAMERA=150: the fight camera keeps 1.5 times its distance.
 *
 * The camera has a mode (+456; 5 is the fight camera, named "CameraKombat") and that mode's update
 * (+0x201e9c) works in spherical terms: the current distance at +624 is eased towards the wanted one at
 * +628 by a helper called at +0x202474, with the wanted distance loaded just before:
 *
 *     20246c  vldr s1, [r4, #628]
 *     202470  vmul.f32 s2, s18, s15        ; s2 is written here, so it is free one instruction earlier
 *
 * The load goes through a stub that multiplies it by the factor.
 */
static void install_fight_camera(so_module *mod, float factor)
{
    uint32_t *site = (uint32_t *)(mod->text_base + 0x20246c);
    uint32_t *stub = *site == 0xedd40a9du
        ? (uint32_t *)so_alloc_arena(mod, 0x01000000, (uintptr_t)site, 5 * sizeof(uint32_t)) : NULL;
    if (!stub) {
        trace("tweaks: fight camera: no distance load at +0x20246c, not the 1.1.6 library - left alone");
        return;
    }
    stub[0] = 0xedd40a9du;          /* vldr s1, [r4, #628]  */
    stub[1] = 0xed9f1a01u;          /* vldr s2, [pc, #4]    */
    stub[2] = 0xee600a81u;          /* vmul.f32 s1, s1, s2  */
    stub[3] = arm_branch((uintptr_t)&stub[3], (uintptr_t)(site + 1));
    memcpy(&stub[4], &factor, sizeof(factor));
    __builtin___clear_cache((char *)stub, (char *)(stub + 5));
    *site = arm_branch((uintptr_t)site, (uintptr_t)stub);
    __builtin___clear_cache((char *)site, (char *)(site + 1));
    trace("tweaks: fight camera: distance x%.2f", factor);
}

void tdkr_tweaks_install(so_module *mod)
{
    long fight = port_getenv_long("FIGHT_CAMERA", 100);
    if (fight != 100 && fight >= 50 && fight <= 400)
        install_fight_camera(mod, fight / 100.0f);

    long cam = port_getenv_long("CAMERA_DISTANCE", 100);
    if (cam != 100 && cam >= 50 && cam <= 300)
        install_camera_distance(mod, cam / 100.0f);

    if (port_getenv_long("FAST_CAS", 1))
        install_fast_cas(mod);

    install_frame_limit(mod);

    if (port_getenv_long("SLEEP_TRACE", 0)) {
        g_trace_mod = mod;
        hook_symbol(mod, "_ZN3glf6Thread5SleepEj", (uintptr_t)sleep_trace_hook, 1);
        trace("tweaks: glf::Thread::Sleep is traced");
    }
}

/*
 * The phone's scheduler puts a utilisation clamp on the game's main thread a few seconds into a
 * mission: uclamp.max 171-199 of 1024, on that one thread and no other. To the frequency governor the
 * thread then never looks more than a fifth loaded, and the core stays at its lowest clock while the
 * thread is in fact saturated (util 1023, 1.0 GHz of 4.3). The clamp is a per-task attribute its owner
 * may set, so the main thread sets it back - again and again, because whatever lowers it does so
 * repeatedly.
 *
 * sched_setattr is not something Android apps normally call, and a system call the seccomp filter
 * does not know kills the process, so the first call is made in a forked child.
 */
struct tdkr_sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t  sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
    uint32_t sched_util_min, sched_util_max;
};

static int set_uclamp(uint32_t lo)
{
    tdkr_sched_attr a;
    memset(&a, 0, sizeof(a));
    a.size = sizeof(a);
    a.sched_flags = 0x08 /* KEEP_POLICY */ | 0x10 /* KEEP_PARAMS */ | 0x20 /* UTIL_CLAMP_MIN */ | 0x40 /* UTIL_CLAMP_MAX */;
    a.sched_util_min = lo;
    a.sched_util_max = 1024;
    return syscall(SYS_sched_setattr, 0, &a, 0) == 0 ? 0 : errno;
}

void tdkr_sched_tick(long frames)
{
    frame_limit_tick();

    static int state = -1;      /* -1 not tried, 0 off, 1 working */
    static uint32_t lo;

    if (state == 0 || (state == 1 && frames % 8 != 0))
        return;
    if (state < 0) {
        state = 0;
        if (!port_getenv_long("UCLAMP", 1))
            return;
        long v = port_getenv_long("UCLAMP_MIN", 0);
        lo = v < 0 ? 0 : v > 1024 ? 1024 : (uint32_t)v;
        pid_t child = fork();
        if (child < 0)
            return;
        if (child == 0)
            _exit(set_uclamp(lo) & 0x7f);
        int st = 0;
        if (waitpid(child, &st, 0) != child || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            trace("tweaks: uclamp: sched_setattr unusable here (%s %d) - left alone",
                  WIFEXITED(st) ? "errno" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
            return;
        }
        state = 1;
        trace("tweaks: uclamp: main thread kept at min %u, max 1024", lo);
    }
    int e = set_uclamp(lo);
    if (e) {
        trace("tweaks: uclamp: sched_setattr failed (errno %d) - stopped", e);
        state = 0;
    }
}
