#import <TargetConditionals.h>

#if TARGET_OS_IOS && !TARGET_OS_SIMULATOR

#include <crt_externs.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

extern int csops(pid_t pid,
                 unsigned int operations,
                 void *user_address,
                 size_t user_size);
extern int ptrace(int request, pid_t pid, caddr_t address, int data);
extern char **environ;

#define PHONEME_CS_OPS_STATUS 0
#define PHONEME_CS_KILL 0x00000200
#define PHONEME_CS_DEBUGGED 0x10000000
#define PHONEME_PT_TRACE_ME 0

static const char *const kPhoneMEJITChildArgument =
    "--phoneme-trollstore-jit-child";

static bool phoneme_process_allows_unsigned_executable_pages(void) {
    int flags = 0;
    if (csops(getpid(), PHONEME_CS_OPS_STATUS, &flags, sizeof(flags)) != 0) {
        return false;
    }

    // A process attached by Xcode, StikDebug, AltJIT or TrollStore is marked
    // CS_DEBUGGED. Jailbroken/AppSync environments can instead remove CS_KILL.
    return (flags & PHONEME_CS_DEBUGGED) != 0 ||
           (flags & PHONEME_CS_KILL) == 0;
}

// Queried through dlsym by Core. This function must never test JIT by jumping
// into unsigned code: on A12+ the kernel can terminate the process at the first
// instruction even when mmap/mprotect appeared to succeed.
__attribute__((used, visibility("default")))
int32_t phoneme_platform_jit_status(void) {
    if (phoneme_process_allows_unsigned_executable_pages()) {
        return 1;
    }
    // Never infer JIT permission from a successful child spawn. posix_spawn()
    // only proves that the helper process was created; PT_TRACE_ME can still
    // fail. Core deliberately trusts this status without executing a probe on
    // A12+, so a false positive here can turn into an immediate AMFI kill when
    // generated ARM64 code is entered. csops is the source of truth.
    return 0;
}

#if defined(PHONEME_TROLLSTORE_BUILD) && PHONEME_TROLLSTORE_BUILD

// UTM-compatible TrollStore bootstrap. A no-sandbox platform application can
// spawn a copy of itself whose only job is PT_TRACE_ME. TrollStore preserves
// the fake entitlements and the parent becomes eligible for unsigned executable
// pages without adding the A12+-banned dynamic-codesigning/debugger entitlements.
__attribute__((constructor, used, visibility("default")))
void phoneme_trollstore_jit_bootstrap_constructor(void) {
    int argc = *_NSGetArgc();
    char **argv = *_NSGetArgv();

    if (argc > 1 && argv != NULL && argv[1] != NULL &&
        strcmp(argv[1], kPhoneMEJITChildArgument) == 0) {
        const int result = ptrace(PHONEME_PT_TRACE_ME, 0, NULL, 0);
        _exit(result == 0 ? 0 : 1);
    }

    setenv("PHONEME_TROLLSTORE_JIT_PACKAGE", "1", 0);

    if (argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        return;
    }

    char *child_argv[] = {
        argv[0],
        (char *)kPhoneMEJITChildArgument,
        NULL,
    };
    pid_t child_pid = 0;
    const int spawn_result = posix_spawn(
        &child_pid,
        argv[0],
        NULL,
        NULL,
        child_argv,
        environ);
    // PT_TRACE_ME may intentionally leave the child in a traced/stopped state,
    // so do not waitpid() it here. JIT readiness is observed independently via
    // csops() by phoneme_platform_jit_status().
    (void)spawn_result;
    (void)child_pid;
}

#endif
#endif
