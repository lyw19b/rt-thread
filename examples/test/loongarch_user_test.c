/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding RT-Thread Smart ABI regression. Linux musl syscall numbers
 * differ, so these wrappers deliberately use the Smart syscall table.
 */
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

/* Indices in components/lwp/lwp_syscall.c's official func_table (numbering starts at 1). */
enum smart_syscall
{
    SMART_SYS_EXIT = 1,
    SMART_SYS_READ = 2,
    SMART_SYS_WRITE = 3,
    SMART_SYS_LSEEK = 4,
    SMART_SYS_OPEN = 5,
    SMART_SYS_CLOSE = 6,
    SMART_SYS_KILL = 14,
    SMART_SYS_GETPID = 15,
    SMART_SYS_SEM_CREATE = 18,
    SMART_SYS_SEM_DELETE = 19,
    SMART_SYS_SEM_TAKE = 20,
    SMART_SYS_SEM_RELEASE = 21,
    SMART_SYS_MUTEX_CREATE = 22,
    SMART_SYS_MUTEX_DELETE = 23,
    SMART_SYS_MUTEX_TAKE = 24,
    SMART_SYS_MUTEX_RELEASE = 25,
    SMART_SYS_THREAD_CREATE = 40,
    SMART_SYS_THREAD_STARTUP = 42,
    SMART_SYS_MMAP2 = 53,
    SMART_SYS_MUNMAP = 54,
    SMART_SYS_MDELAY = 102,
    SMART_SYS_SIGACTION = 103,
    SMART_SYS_SIGPROCMASK = 104,
    SMART_SYS_WAITPID = 110,
    SMART_SYS_UNLINK = 118,
    SMART_SYS_SET_THREAD_AREA = 123,
    SMART_SYS_FORK = 136,
    SMART_SYS_EXECVE = 137,
};

static long syscall6(long nr, long x0, long x1, long x2, long x3, long x4, long x5)
{
    register long a0 __asm__("a0") = x0;
    register long a1 __asm__("a1") = x1;
    register long a2 __asm__("a2") = x2;
    register long a3 __asm__("a3") = x3;
    register long a4 __asm__("a4") = x4;
    register long a5 __asm__("a5") = x5;
    register long a7 __asm__("a7") = nr;
    __asm__ volatile("syscall 0" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a7) : "memory");
    return a0;
}
#define CALL(n, a, b, c) syscall6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

static void puts_test(const char *text)
{
    size_t length = 0;
    while (text[length]) length++;
    CALL(SMART_SYS_WRITE, 1, text, length);
}

static void exit_test(int status)
{
    CALL(SMART_SYS_EXIT, status, 0, 0);
    for (;;) {}
}

static void fail(const char *text)
{
    puts_test("SMART FAIL: ");
    puts_test(text);
    puts_test("\n");
    exit_test(1);
}
#define CHECK(expr) do { if (!(expr)) fail(#expr); } while (0)

/* A previously exited child's SIGCHLD can interrupt the next wait. */
static long wait_child(long pid, int *status)
{
    long result;
    do
    {
        result = CALL(SMART_SYS_WAITPID, pid, status, 0);
    } while (result == -EINTR);
    return result;
}

static volatile int signal_count;
static volatile int cloned;
static volatile long clone_tls_seen;
static long parent_tls, child_tls;
static unsigned char clone_stack[16384] __attribute__((aligned(16)));

static long get_tp(void)
{
    long value;
    __asm__ volatile("move %0, $tp" : "=r"(value));
    return value;
}

static void signal_handler(int signal)
{
    signal_count += signal == SIGUSR1;
    __asm__ volatile("movgr2fr.d $f0, $zero; movgr2fr.d $f23, $zero" ::: "$f0", "$f23");
}

struct smart_sigaction
{
    void (*handler)(int);
    unsigned long flags;
    void (*restorer)(void);
    unsigned int mask[2];
};

static void test_signal(void)
{
    struct smart_sigaction action = {.handler = signal_handler};
    unsigned long mask = 1UL << (SIGUSR1 - 1);
    CHECK(syscall6(SMART_SYS_SIGACTION, SIGUSR1, (long)&action, 0, 8, 0, 0) == 0);
    CHECK(syscall6(SMART_SYS_SIGPROCMASK, SIG_BLOCK, (long)&mask, 0, 8, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_KILL, CALL(SMART_SYS_GETPID, 0, 0, 0), SIGUSR1, 0) == 0);
    CHECK(signal_count == 0);
    CHECK(syscall6(SMART_SYS_SIGPROCMASK, SIG_UNBLOCK, (long)&mask, 0, 8, 0, 0) == 0);
    CHECK(signal_count == 1);
    CHECK(CALL(SMART_SYS_KILL, CALL(SMART_SYS_GETPID, 0, 0, 0), SIGUSR1, 0) == 0);
    CHECK(signal_count == 2);
    puts_test("SMART signal PASS\n");
}

static void test_fpu(void)
{
    unsigned long old_fcsr, seen_fcsr, first, last;
    const unsigned long bits = 0x3ff8000000000000UL;
    unsigned long rounding = 1UL << 8;
    int i, condition;
    __asm__ volatile("movfcsr2gr %0, $fcsr0" : "=r"(old_fcsr));
    for (i = 0; i < 128; i++)
    {
        __asm__ volatile("movgr2fr.d $f0, %0; movgr2fr.d $f23, %0;"
                         "movgr2fcsr $fcsr0, %1; movgr2cf $fcc3, %2"
                         :: "r"(bits), "r"(rounding), "r"(1) : "$f0", "$f23", "memory");
        CALL(SMART_SYS_KILL, CALL(SMART_SYS_GETPID, 0, 0, 0), SIGUSR1, 0);
        CALL(SMART_SYS_MDELAY, 2, 0, 0);
        __asm__ volatile("movfr2gr.d %0, $f0; movfr2gr.d %1, $f23;"
                         "movfcsr2gr %2, $fcsr0; movcf2gr %3, $fcc3"
                         : "=r"(first), "=r"(last), "=r"(seen_fcsr), "=r"(condition));
        CHECK(first == bits && last == bits && seen_fcsr == rounding && condition == 1);
    }
    __asm__ volatile("movgr2fcsr $fcsr0, %0" :: "r"(old_fcsr));
    puts_test("SMART FPU signal and preemption PASS\n");
}

extern long loongarch_test_clone(void *args);
extern void clone_child(void);
void clone_child(void)
{
    int i;
    clone_tls_seen = get_tp();
    for (i = 0; i < 100; i++) CALL(SMART_SYS_MDELAY, 1, 0, 0);
    __atomic_store_n(&cloned, 1, __ATOMIC_RELEASE);
    exit_test(0);
}

static void test_clone(void)
{
    long args[5] = {0};
    long *top = (long *)(clone_stack + sizeof(clone_stack));
    int i;
    args[0] = 0x100 | 0x200 | 0x400 | 0x800 | 0x10000 | 0x40000 | 0x80000;
    args[1] = (long)top;
    args[3] = (long)&child_tls;
    CHECK(CALL(SMART_SYS_SET_THREAD_AREA, &parent_tls, 0, 0) == 0);
    CHECK(get_tp() == (long)&parent_tls);
    CHECK(loongarch_test_clone(args) > 0);
    for (i = 0; i < 1000 && !__atomic_load_n(&cloned, __ATOMIC_ACQUIRE); i++) CALL(SMART_SYS_MDELAY, 1, 0, 0);
    CHECK(cloned && clone_tls_seen == (long)&child_tls);
    CHECK(get_tp() == (long)&parent_tls);
    puts_test("SMART clone TLS PASS\n");
}

static volatile int rt_thread_done;

static int rt_user_thread(void *parameter)
{
    CHECK(parameter == (void *)0x1234);
    __atomic_store_n(&rt_thread_done, 1, __ATOMIC_RELEASE);
    return 0;
}

static void test_rt_ipc_thread(void)
{
    long sem = CALL(SMART_SYS_SEM_CREATE, "usersem", 0, 0);
    long mutex = CALL(SMART_SYS_MUTEX_CREATE, "usermtx", 0, 0);
    long args[] = {(long)"userthread", (long)rt_user_thread, 0x1234, 8192, 20, 10};
    long thread;
    int i;
    CHECK(sem && mutex);
    CHECK(CALL(SMART_SYS_SEM_TAKE, sem, 1, 0) < 0);
    CHECK(CALL(SMART_SYS_SEM_RELEASE, sem, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_SEM_TAKE, sem, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_MUTEX_TAKE, mutex, -1, 0) == 0);
    CHECK(CALL(SMART_SYS_MUTEX_RELEASE, mutex, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_SEM_DELETE, sem, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_MUTEX_DELETE, mutex, 0, 0) == 0);
    thread = CALL(SMART_SYS_THREAD_CREATE, args, 0, 0);
    CHECK(thread);
    CHECK(CALL(SMART_SYS_THREAD_STARTUP, thread, 0, 0) == 0);
    for (i = 0; i < 1000 && !__atomic_load_n(&rt_thread_done, __ATOMIC_ACQUIRE); i++) CALL(SMART_SYS_MDELAY, 1, 0, 0);
    CHECK(rt_thread_done);
    CALL(SMART_SYS_MDELAY, 10, 0, 0);
    puts_test("SMART RT IPC thread return PASS\n");
}

static void test_file(void)
{
    const char content[] = "loongarch file roundtrip";
    char buffer[sizeof(content)] = {0};
    long fd = CALL(SMART_SYS_OPEN, "/roundtrip", O_CREAT | O_TRUNC | O_RDWR, 0600);
    unsigned int i;
    CHECK(fd >= 0);
    CHECK(CALL(SMART_SYS_WRITE, fd, content, sizeof(content)) == sizeof(content));
    CHECK(CALL(SMART_SYS_LSEEK, fd, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_READ, fd, buffer, sizeof(buffer)) == sizeof(buffer));
    for (i = 0; i < sizeof(content); i++) CHECK(buffer[i] == content[i]);
    CHECK(CALL(SMART_SYS_CLOSE, fd, 0, 0) == 0);
    CHECK(CALL(SMART_SYS_UNLINK, "/roundtrip", 0, 0) == 0);
    CHECK(CALL(SMART_SYS_WRITE, 1, 0x9000000000200000UL, 4) < 0);
    CHECK(CALL(SMART_SYS_WRITE, 1, 0xffffffe000200000UL, 4) < 0);
    puts_test("SMART file and invalid pointer PASS\n");
}

static void test_memory_fork(int loops)
{
    int iteration;
    for (iteration = 0; iteration < loops; iteration++)
    {
        long address = syscall6(SMART_SYS_MMAP2, 0, 16 * 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        volatile unsigned long *memory = (void *)address;
        long pid;
        size_t i;
        int status = -1;
        CHECK(address > 0);
        for (i = 0; i < 16 * 4096 / sizeof(long); i++) memory[i] = i + iteration;
        pid = CALL(SMART_SYS_FORK, 0, 0, 0);
        CHECK(pid >= 0);
        if (!pid)
        {
            for (i = 0; i < 16 * 4096 / sizeof(long); i++)
            {
                CHECK(memory[i] == i + iteration);
                memory[i] = 0x987654321UL;
            }
            exit_test(37);
        }
        CHECK(wait_child(pid, &status) == pid);
        CHECK(status == (37 << 8));
        for (i = 0; i < 16 * 4096 / sizeof(long); i++) CHECK(memory[i] == i + iteration);
        CHECK(CALL(SMART_SYS_MUNMAP, address, 16 * 4096, 0) == 0);
        /* waitpid reports exit before the lower-priority tsystem thread reaps
         * resources. Bound the backlog without reducing the fork/CoW load. */
        if ((iteration + 1) % 32 == 0)
            CALL(SMART_SYS_MDELAY, 20, 0, 0);
    }
    puts_test("SMART mmap fork CoW wait PASS\n");
}

static void test_fault(void)
{
    int kind;
    for (kind = 0; kind < 8; kind++)
    {
        long pid = CALL(SMART_SYS_FORK, 0, 0, 0);
        int status = 0;
        CHECK(pid >= 0);
        if (!pid)
        {
            switch (kind)
            {
            case 0:
                *(volatile long *)0x9000000000200000UL = 42;
                break;
            case 1:
                /* Explicit store below USER_VADDR_START, without C null-object UB. */
                __asm__ volatile("st.d %0, %1, 0" :: "r"(42L), "r"(0x100UL) : "memory");
                break;
            case 2:
                __asm__ volatile("csrrd $t0, 0" ::: "$t0");
                break;
            case 3:
                __asm__ volatile("break 0");
                break;
            case 4:
                __asm__ volatile(".word 0");
                break;
            case 5:
                __asm__ volatile("asrtle.d %0, %1" ::"r"(1L), "r"(0L));
                break;
            case 6:
                *(volatile long *)0xffffffe000200000UL = 42;
                break;
            case 7:
                *(volatile long *)0xffffffe116100000UL = 42;
                break;
            }
            exit_test(0);
        }
        CHECK(wait_child(pid, &status) == pid);
        CHECK(status != 0);
    }
    puts_test("SMART fault isolation PASS\n");
}

static void test_exec(void)
{
    long pid = CALL(SMART_SYS_FORK, 0, 0, 0);
    int status = -1;
    CHECK(pid >= 0);
    if (!pid)
    {
        char *args[] = {"/user.elf", "exec-child", 0};
        char *env[] = {0};
        CALL(SMART_SYS_EXECVE, args[0], args, env);
        fail("execve returned");
    }
    CHECK(wait_child(pid, &status) == pid);
    CHECK(status == (23 << 8));
    puts_test("SMART exec PASS\n");
}

static int positive_number(const char *text, int limit)
{
    int value = 0;
    while (*text >= '0' && *text <= '9' && value <= limit)
        value = value * 10 + *text++ - '0';
    CHECK(!*text && value > 0 && value <= limit);
    return value;
}

static void test_parallel_processes(int workers, int loops)
{
    long children[16];
    int i;
    int done;
    int batch_limit = 32 / workers;

    /* Bound the defunct backlog. waitpid observes exit before the low-priority
     * kernel reaper has freed the stack and address space. Between batches all
     * workers stop, leaving CPUs available for reclamation. */
    for (done = 0; done < loops; done += batch_limit)
    {
        int batch = loops - done < batch_limit ? loops - done : batch_limit;
        for (i = 0; i < workers; i++)
        {
            children[i] = CALL(SMART_SYS_FORK, 0, 0, 0);
            CHECK(children[i] >= 0);
            if (!children[i])
            {
                test_memory_fork(batch);
                test_exec();
                exit_test(37);
            }
        }
        for (i = 0; i < workers; i++)
        {
            int status = -1;
            CHECK(wait_child(children[i], &status) == children[i]);
            CHECK(status == (37 << 8));
        }
        CALL(SMART_SYS_MDELAY, 100, 0, 0);
    }
    puts_test("SMART PARALLEL PASS\n");
}

void user_main(long *args)
{
    long argc = args[0];
    char **argv = (char **)&args[1];
    int loops = 8;
    if (argc > 1 && argv[1][0] == 'e') exit_test(23);
    if (argc > 1 && argv[1][0] == 'p')
    {
        CHECK(argc == 4);
        test_parallel_processes(positive_number(argv[2], 16), positive_number(argv[3], 100000));
        exit_test(0);
    }
    if (argc > 1 && argv[1][0] == 's')
    {
        loops = 256;
        if (argc > 2)
        {
            const char *number = argv[2];
            loops = 0;
            while (*number >= '0' && *number <= '9' && loops < 100000)
                loops = loops * 10 + *number++ - '0';
            CHECK(loops > 0 && loops <= 100000);
        }
    }
    CHECK(CALL(SMART_SYS_GETPID, 0, 0, 0) > 0);
    CHECK(CALL(9999, 0, 0, 0) < 0);
    puts_test("SMART syscall PASS\n");
    test_file();
    test_signal();
    test_fpu();
    test_clone();
    test_rt_ipc_thread();
    test_memory_fork(loops);
    test_fault();
    test_exec();
    puts_test(loops != 8 ? "SMART STRESS PASS\n" : "SMART REGRESSION PASS\n");
    exit_test(0);
}
