/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-27     LoongsonLab               adapt reference BSP to project APIs
 */

/*
 * Description:
 *   LS2K0300 development BSP: test erestart.
 *   Imported from the reference LoongArch BSP; see IMPORT_NOTES.txt.
 */

/*
 * ERESTART syscall restart test for rt-thread-smart (loongarch64).
 *
 * Tests that blocking syscalls are automatically restarted after a signal
 * handler returns, when SA_RESTART is set. Also verifies that without
 * SA_RESTART the syscall returns -EINTR.
 *
 * Build: see Makefile (loongarch64-linux-musl-gcc -static)
 * Run:   load the ELF via the RT-Thread LWP exec command on the 2K0300 board.
 *
 * Test cases:
 *   1. read() on a pipe with SA_RESTART → should restart, return data
 *   2. read() on a pipe WITHOUT SA_RESTART → should return -EINTR
 *   3. nanosleep() with SA_RESTART → should restart, complete full duration
 */

#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/types.h>

static volatile sig_atomic_t g_handler_called;

static void sigusr1_handler(int signo)
{
    (void)signo;
    g_handler_called = 1;
}

static void install_handler(int signo, int use_restart)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = use_restart ? SA_RESTART : 0;
    sigaction(signo, &sa, NULL);
}

/* ----------------------------------------------------------------------- */
/* Test 1: read() on a pipe with SA_RESTART should be restarted            */
/* ----------------------------------------------------------------------- */
static int test_read_restart(void)
{
    int pipefd[2];
    char buf[64];
    ssize_t n;

    printf("[test_read_restart] start\n");

    if (pipe(pipefd) < 0)
    {
        printf("  FAIL: pipe: %s\n", strerror(errno));
        return 1;
    }

    install_handler(SIGUSR1, 1);
    g_handler_called = 0;

    pid_t pid = fork();
    if (pid < 0)
    {
        printf("  FAIL: fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0)
    {
        /* child: signal parent, then write to pipe */
        close(pipefd[0]);
        usleep(100 * 1000);              /* 100 ms — let parent block in read */
        kill(getppid(), SIGUSR1);
        usleep(50 * 1000);               /* 50 ms — handler returns, read restarts */
        write(pipefd[1], "hello", 5);
        close(pipefd[1]);
        _exit(0);
    }

    /* parent: block in read, get interrupted, should restart */
    close(pipefd[1]);
    n = read(pipefd[0], buf, sizeof(buf) - 1);

    if (n < 0)
    {
        printf("  FAIL: read returned %zd, errno=%d (%s)\n", n, errno, strerror(errno));
        if (errno == EINTR)
        {
            printf("  FAIL: read was NOT restarted — ERESTART broken\n");
        }
        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        return 1;
    }

    buf[n] = '\0';
    int pass = (n == 5 && memcmp(buf, "hello", 5) == 0 && g_handler_called);

    printf("  read returned %zd bytes: '%s'\n", n, buf);
    printf("  handler called: %s\n", g_handler_called ? "yes" : "no");
    printf("  %s\n", pass ? "PASS" : "FAIL");

    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    return pass ? 0 : 1;
}

/* ----------------------------------------------------------------------- */
/* Test 2: read() on a pipe WITHOUT SA_RESTART should return -EINTR        */
/* ----------------------------------------------------------------------- */
static int test_read_eintr(void)
{
    int pipefd[2];
    char buf[64];
    ssize_t n;

    printf("[test_read_eintr] start\n");

    if (pipe(pipefd) < 0)
    {
        printf("  FAIL: pipe: %s\n", strerror(errno));
        return 1;
    }

    install_handler(SIGUSR1, 0);          /* no SA_RESTART */
    g_handler_called = 0;

    pid_t pid = fork();
    if (pid < 0)
    {
        printf("  FAIL: fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0)
    {
        close(pipefd[0]);
        usleep(100 * 1000);
        kill(getppid(), SIGUSR1);
        /* don't write — parent should get EINTR and exit */
        usleep(200 * 1000);
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    n = read(pipefd[0], buf, sizeof(buf));

    int pass = (n < 0 && errno == EINTR && g_handler_called);

    printf("  read returned %zd, errno=%d (%s)\n", n, errno, strerror(errno));
    printf("  handler called: %s\n", g_handler_called ? "yes" : "no");
    printf("  %s\n", pass ? "PASS" : "FAIL");

    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    return pass ? 0 : 1;
}

/* ----------------------------------------------------------------------- */
/* Test 3: nanosleep() with SA_RESTART should complete the full duration   */
/* (nanosleep may or may not support ERESTART — if it returns -EINTR with  */
/*  a remaining time, that's also acceptable per POSIX)                    */
/* ----------------------------------------------------------------------- */
static int test_nanosleep_restart(void)
{
    struct timespec req, rem;
    int ret;

    printf("[test_nanosleep_restart] start\n");

    install_handler(SIGUSR1, 1);
    g_handler_called = 0;

    pid_t pid = fork();
    if (pid < 0)
    {
        printf("  FAIL: fork: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0)
    {
        usleep(100 * 1000);               /* 100 ms */
        kill(getppid(), SIGUSR1);
        _exit(0);
    }

    req.tv_sec = 0;
    req.tv_nsec = 300 * 1000 * 1000;      /* 300 ms */
    memset(&rem, 0, sizeof(rem));

    ret = nanosleep(&req, &rem);

    int pass;
    if (ret == 0)
    {
        /* slept the full duration — either not interrupted, or restarted */
        pass = g_handler_called;
        printf("  nanosleep completed fully, handler called: %s\n",
               g_handler_called ? "yes" : "no");
    }
    else if (ret < 0 && errno == EINTR)
    {
        /* interrupted and not restarted — acceptable if rem is set */
        pass = (rem.tv_nsec > 0 || rem.tv_sec > 0);
        printf("  nanosleep interrupted, remaining: %ld.%09lds\n", (long)rem.tv_sec, rem.tv_nsec);
        printf("  (nanosleep does not support ERESTART — EINTR with remainder)\n");
    }
    else
    {
        pass = 0;
        printf("  unexpected: ret=%d errno=%d (%s)\n", ret, errno, strerror(errno));
    }

    printf("  %s\n", pass ? "PASS" : "FAIL");

    waitpid(pid, NULL, 0);
    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int failures = 0;

    printf("=== ERESTART syscall restart tests ===\n");
    printf("pid=%d\n", (int)getpid());

    failures += test_read_restart();
    failures += test_read_eintr();
    failures += test_nanosleep_restart();

    printf("\n=== Results: %d failure(s) ===\n", failures);
    if (failures == 0)
    {
        printf("ALL TESTS PASSED\n");
    }
    else
    {
        printf("SOME TESTS FAILED\n");
    }

    return failures ? 1 : 0;
}
