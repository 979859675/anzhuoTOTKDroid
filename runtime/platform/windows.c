// Host OS interface for the Windows (x64) runtime: the functions of runtime/platform.h that platform/android.c has for
// Android. Written from the Windows API; not run yet (docs/PC-BUILD.md). The parts that still need a Windows machine
// to confirm are marked TESTME. Write tracking, the crash backtrace and the device statistics are not done: each is
// reported as unavailable (the runtime already handles that).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>  // timeBeginPeriod (link winmm)
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../paths.h"
#include "../platform.h"
#include "../rt.h"

// The guest window's start on the desktop (rt.h GUEST_CANON): the game's own addresses are made for it.
#define WIN_GUEST_BASE 0x7000000000ull

uint64_t g_guest_lo;  // the window's start (rt.h GUEST_LO), set by plat_reserve_guest before main()

// ---- memory
// The guest window is reserved (address space only) and committed where the runtime uses it: plat_commit, and fresh
// ranges from plat_discard / plat_reset_guest. TESTME: Windows has no lazily backed private mapping like Linux's,
// so an access to a reserved-only page is an access violation; any range the loader writes without a plat_commit
// shows up as one of those.
bool plat_reserve_guest(uint64_t lo, uint64_t hi) {
    if (!g_guest_lo) {
        void *p = VirtualAlloc((void *)(uintptr_t)WIN_GUEST_BASE, hi - lo, MEM_RESERVE, PAGE_NOACCESS);
        if (!p) {
            fprintf(stderr, "FATAL: cannot reserve the guest window at %#llx (%#llx bytes), error %lu\n",
                    (unsigned long long)WIN_GUEST_BASE, (unsigned long long)(hi - lo), GetLastError());
            return false;
        }
        g_guest_lo = (uint64_t)(uintptr_t)p;
        fprintf(stderr, "[windows] guest window %#llx-%#llx\n", (unsigned long long)g_guest_lo,
                (unsigned long long)(g_guest_lo + hi - lo));
        return true;
    }
    // a range outside the window (the first reservation's): reserved at its own address
    void *p = VirtualAlloc((void *)(uintptr_t)lo, hi - lo, MEM_RESERVE, PAGE_NOACCESS);
    return p == (void *)(uintptr_t)lo;
}

// Fresh zero pages over [lo, hi): decommitted, then committed again (the OS zeroes them).
bool plat_reset_guest(uint64_t lo, uint64_t hi) {
    VirtualFree((void *)(uintptr_t)lo, hi - lo, MEM_DECOMMIT);
    return VirtualAlloc((void *)(uintptr_t)lo, hi - lo, MEM_COMMIT, PAGE_READWRITE) == (void *)(uintptr_t)lo;
}

// Backs [addr, addr+size) of the window before the runtime (or the OS, for a read into it) writes there.
void plat_commit(uint64_t addr, uint64_t size) {
    uint64_t pg = 0x1000, lo = addr & ~(pg - 1), hi = (addr + size + pg - 1) & ~(pg - 1);
    if (hi > lo) VirtualAlloc((void *)(uintptr_t)lo, hi - lo, MEM_COMMIT, PAGE_READWRITE);
}

// [addr, addr+size) back to zero: committed (so it reads) and cleared by hand.
void plat_discard(uint64_t addr, uint64_t size) {
    extern bool vk_vamm_clear(uint64_t addr, uint64_t size);
    if (vk_vamm_clear(addr, size)) return;
    plat_commit(addr, size);
    memset((void *)(uintptr_t)addr, 0, size);
}

// Page write tracking is not implemented on Windows yet: the callers take "no tracking" (-1) and treat every page as
// written (plat_written_pages, runtime/nvn/vulkan/async.c).
void plat_write_tracking_enable(uint64_t lo, uint64_t hi) { (void)lo; (void)hi; }
void plat_write_prepare(uint64_t addr, uint64_t size) { (void)addr; (void)size; }
int plat_written_pages(uint64_t addr, uint64_t size, uint64_t *pages, int max, uint64_t *page) {
    (void)addr; (void)size; (void)pages; (void)max; (void)page;
    return -1;
}

int plat_protect(uint64_t addr, uint64_t size, int prot) {
    DWORD p = prot == PLAT_PROT_NONE ? PAGE_NOACCESS : prot == PLAT_PROT_READ ? PAGE_READONLY : PAGE_READWRITE;
    DWORD old;
    return VirtualProtect((void *)(uintptr_t)addr, size, p, &old) ? 0 : -1;
}

// ---- time
static uint64_t qpc_freq(void) {
    static LARGE_INTEGER f;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    return (uint64_t)f.QuadPart;
}

uint64_t plat_now_ns(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    uint64_t f = qpc_freq();
    uint64_t s = (uint64_t)c.QuadPart / f;
    return s * 1000000000ull + ((uint64_t)c.QuadPart % f) * 1000000000ull / f;
}

// Sleeps (coarse) until close to t, then spins for the rest.
void plat_sleep_until_ns(uint64_t t) {
    for (;;) {
        uint64_t now = plat_now_ns();
        if (now >= t) return;
        uint64_t left = t - now;
        if (left > 2000000ull) Sleep((DWORD)((left - 1000000ull) / 1000000ull));
    }
}

// The 1 ms timer resolution the frame pacing needs (TESTME: timeBeginPeriod is process-wide and stays on).
void plat_thread_realtime(uint64_t period_ns) {
    (void)period_ns;
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
}

// ---- process
void plat_init(void) {}

bool plat_exe_path(char *buf, size_t size) {
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size);
    return n > 0 && n < size;
}

void plat_exec(char *const argv[]) { (void)argv; }  // not on Windows: the caller reports the failure

void plat_terminate(void) { ExitProcess(0); }

// Child processes (the game code generator): the command line is the arguments, quoted; the output goes to log_path.
PlatProc plat_spawn(const char *const argv[], const char *log_path, bool append_log) {
    char cmd[8192] = "";
    for (int i = 0; argv[i]; i++) {
        char q[1024];
        int n = snprintf(q, sizeof q, strpbrk(argv[i], " \t\"") ? "\"%s\"" : "%s", argv[i]);
        if (n < 0 || strlen(cmd) + n + 2 >= sizeof cmd) return 0;
        strcat(cmd, i ? " " : "");
        strcat(cmd, q);
    }
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE out = INVALID_HANDLE_VALUE;
    if (log_path) {
        out = CreateFileA(log_path, FILE_APPEND_DATA, FILE_SHARE_READ, &sa, append_log ? OPEN_ALWAYS : CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
        if (out == INVALID_HANDLE_VALUE) return 0;
    }
    STARTUPINFOA si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = out != INVALID_HANDLE_VALUE ? out : GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = si.hStdOutput;
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return 0;
    CloseHandle(pi.hThread);
    return (PlatProc)pi.hProcess;
}

int plat_proc_wait(PlatProc p, bool block) {
    HANDLE h = (HANDLE)p;
    if (WaitForSingleObject(h, block ? INFINITE : 0) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeProcess(h, &code);
    return (int)code;
}

void plat_proc_kill(PlatProc p) {
    TerminateProcess((HANDLE)p, 1);
    CloseHandle((HANDLE)p);
}

static bool remove_tree_rec(const char *dir) {
    char pat[MAX_PATH], path[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return true;  // (gone, or nothing to remove)
    bool ok = true;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = remove_tree_rec(path) && ok;
        else ok = DeleteFileA(path) && ok;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return RemoveDirectoryA(dir) && ok;
}

bool plat_remove_tree(const char *dir) { return remove_tree_rec(dir); }

// ---- diagnostics
void rt_fatal_message(const char *format, ...) {
    char msg[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof msg, format, args);
    va_end(args);
    fprintf(stderr, "FATAL: %s", msg);
    plat_fatal_notice();
    ExitProcess(1);
}

const char *plat_symbol(uint64_t pc, uint64_t *offset) {
    (void)pc; (void)offset;
    return NULL;  // TESTME: the names come from the generated game code's symbol table (not on Windows yet)
}

void plat_print_backtrace(void) { fprintf(stderr, "(no backtrace on Windows yet)\n"); }

void plat_fatal_notice(void) {
    fflush(stderr);
    MessageBoxA(NULL, "TOTKDroid stopped because of an error. See the log (totkrecomp-log.txt) for details.", "TOTKDroid",
                MB_ICONERROR | MB_OK);
}

// The unhandled exceptions are logged with their address, then the process ends (no fault watch yet: rt_fault_watch).
static LONG WINAPI unhandled(EXCEPTION_POINTERS *ep) {
    fprintf(stderr, "FATAL: exception %#lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}

void plat_crash_handler_install(void) { SetUnhandledExceptionFilter(unhandled); }

bool rt_fault_watch(uint64_t fault_addr, uint64_t host_pc) {
    (void)fault_addr; (void)host_pc;
    return false;
}

void rt_fault_report(const char *what, uint64_t fault_addr, uint64_t host_pc, uint64_t host_lr) {
    fprintf(stderr, "FATAL: %s at %#llx (pc %#llx, lr %#llx)\n", what, (unsigned long long)fault_addr,
            (unsigned long long)host_pc, (unsigned long long)host_lr);
    plat_fatal_notice();
    ExitProcess(1);
}

// ---- device state and ADPF: Android only. Reported as unavailable here.
void plat_device_stats(PlatDeviceStats *out) { memset(out, 0, sizeof *out); }
void plat_frame_work(uint64_t work_ns, uint64_t target_ns) { (void)work_ns; (void)target_ns; }
void plat_frame_work_gpu(uint64_t cpu_ns, uint64_t gpu_ns, uint64_t target_ns, uint64_t game_ns) {
    (void)cpu_ns; (void)gpu_ns; (void)target_ns; (void)game_ns;
}
void plat_adpf_extra_threads(const int32_t *tids, int n) { (void)tids; (void)n; }
void plat_adpf_stats(PlatAdpfStats *out) {
    memset(out, 0, sizeof *out);
    out->state = -1;
    out->game_cpu = out->render_cpu = -1;
}
void plat_thread_unpin(void) {}
void plat_render_follow_clock(void) {}
void plat_render_thread_pin(void) {}
void plat_render_work(uint64_t work_ns, uint64_t target_ns) { (void)work_ns; (void)target_ns; }
void android_log_file(bool on) { (void)on; }  // (the log file: the desktop's own, later)
