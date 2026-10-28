/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include "userland/haiku_compat.h"
#include <OS.h>
#include <FindDirectory.h>
#include <Architecture.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <locks.h>
#include <pthread.h>
#include <atomic>
#include <fs_info.h>
#include <sys/statvfs.h>

extern "C" area_id _kern_transfer_area(area_id area, void** address,
    uint32 addressSpec, team_id target);

static bigtime_t linux_cpu0_busy_time()
{
    FILE* file = fopen("/proc/stat", "r");
    assert(file);
    char line[1024];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "cpu0 ", 5) != 0)
            continue;
        unsigned long long ticks[8];
        assert(sscanf(line + 5, "%llu %llu %llu %llu %llu %llu %llu %llu",
            &ticks[0], &ticks[1], &ticks[2], &ticks[3], &ticks[4], &ticks[5],
            &ticks[6], &ticks[7]) == 8);
        fclose(file);
        // user, nice, system, IRQ and softIRQ; exclude idle, I/O wait and steal.
        return (ticks[0] + ticks[1] + ticks[2] + ticks[5] + ticks[6])
            * 1000000ULL / sysconf(_SC_CLK_TCK);
    }
    fclose(file);
    assert(false && "CPU 0 must be present in the QEMU guest");
    return -1;
}

static void test_system_info(void)
{
    printf("[TEST] system_info...\n");
    system_info info;
    status_t st = get_system_info(&info);
    assert(st == B_OK);
    assert(info.cpu_count >= 1);
    assert(info.max_pages > 0);
    assert(info.free_memory > 0);
    assert(info.cpu_count == (uint32)sysconf(_SC_NPROCESSORS_ONLN));
    const char* architecture = nullptr;
    assert(get_architectures(&architecture, 1) == 1);
    assert(strcmp(architecture, __HAIKU_ARCH_ABI) == 0);
    struct stat root;
    struct statvfs native_fs;
    fs_info filesystem;
    assert(stat("/", &root) == 0 && statvfs("/", &native_fs) == 0);
    dev_t root_device = dev_for_path("/");
    assert(root_device == (dev_t)root.st_dev);
    assert(fs_stat_dev(root_device, &filesystem) == B_OK);
    assert(filesystem.root == (ino_t)root.st_ino);
    assert(filesystem.total_blocks == (off_t)native_fs.f_blocks);
    assert(filesystem.total_nodes == (off_t)native_fs.f_files);
    assert(filesystem.block_size == (off_t)(native_fs.f_frsize ? native_fs.f_frsize : native_fs.f_bsize));
    assert(dev_for_path("/this-path-must-not-exist-in-test-image") < 0);

    cpu_info cpu;
    const bigtime_t busy_before = linux_cpu0_busy_time();
    assert(get_cpu_info(0, 1, &cpu) == B_OK);
    const bigtime_t busy_after = linux_cpu0_busy_time();
    assert(cpu.enabled);
    assert(cpu.active_time >= 0 && cpu.active_time <= system_time());
    assert(cpu.active_time >= busy_before && cpu.active_time <= busy_after);
    printf("       cpu0 active_time=%lld us matches live /proc/stat bounds (%lld..%lld)\n",
        (long long)cpu.active_time, (long long)busy_before, (long long)busy_after);
    FILE* frequency = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    if (!frequency)
        assert(cpu.current_frequency == 0);
    else
        fclose(frequency);
    assert(get_cpu_info(UINT_MAX, 1, &cpu) == B_BAD_VALUE);

    team_info team;
    assert(get_team_info(B_CURRENT_TEAM, &team) == B_OK);
    assert(team.team == getpid() && team.thread_count >= 1);
    assert(team.uid == geteuid() && team.gid == getegid());
    assert(team.name[0]);
    // A reaped child must cease to be reported as a live team.
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0)
        _exit(0);
    assert(waitpid(child, nullptr, 0) == child);
    assert(get_team_info(child, &team) == B_BAD_TEAM_ID);

    bigtime_t t1 = system_time();
    snooze(10000); // 10 ms
    bigtime_t t2 = system_time();
    assert(t2 > t1);
    printf("       cpu_count=%u, max_pages=%llu, system_time=%llu us: PASS\n",
           info.cpu_count, (unsigned long long)info.max_pages, (unsigned long long)t2);
}

static void test_areas(void)
{
    printf("[TEST] areas...\n");
    void* addr = nullptr;
    area_id id = create_area("test_area", &addr, B_ANY_ADDRESS, 8192,
                             B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
    assert(id > 0);
    assert(addr != nullptr);

    uint32_t* ptr = (uint32_t*)addr;
    ptr[0] = 0xDEADBEEF;
    void* transferred = nullptr;
    assert(_kern_transfer_area(id, &transferred, B_ANY_ADDRESS, getpid()) == B_NOT_SUPPORTED);
    assert(ptr[0] == 0xDEADBEEF); // Unsupported transfer must leave the source intact.

    void* clone_addr = nullptr;
    area_id clone_id = clone_area("cloned_area", &clone_addr, B_ANY_ADDRESS,
                                  B_READ_AREA | B_WRITE_AREA, id);
    assert(clone_id > 0);
    assert(clone_addr != nullptr);
    uint32_t* clone_ptr = (uint32_t*)clone_addr;
    assert(clone_ptr[0] == 0xDEADBEEF);

    // Mutate through clone and verify in original
    clone_ptr[0] = 0xCAFEBABE;
    assert(ptr[0] == 0xCAFEBABE);

    assert(find_area("test_area") == id);
    assert(find_area("cloned_area") == clone_id);

    assert(delete_area(id) == B_OK);
    assert(delete_area(clone_id) == B_OK);
    printf("       create_area, clone_area, shared memory mutation, delete_area: PASS\n");
}

static void test_ports(void)
{
    printf("[TEST] ports...\n");
    port_id port = create_port(16, "test_port");
    assert(port > 0);
    assert(find_port("test_port") == port);

    const char* test_msg = "hello haiku port";
    status_t st = write_port(port, 42, test_msg, strlen(test_msg) + 1);
    assert(st == B_OK);
    assert(port_count(port) == 1);

    int32 code = 0;
    char buf[128];
    ssize_t sz = read_port(port, &code, buf, sizeof(buf));
    assert(sz == (ssize_t)(strlen(test_msg) + 1));
    assert(code == 42);
    assert(strcmp(buf, test_msg) == 0);
    assert(port_count(port) == 0);

    // Test timeout on empty port
    sz = read_port_etc(port, &code, buf, sizeof(buf), B_RELATIVE_TIMEOUT, 5000);
    assert(sz == B_TIMED_OUT);

    assert(delete_port(port) == B_OK);
    printf("       create_port, find_port, write_port, read_port, timeout: PASS\n");
}

static void test_sems(void)
{
    printf("[TEST] semaphores...\n");
    sem_id sem = create_sem(1, "test_sem");
    assert(sem > 0);

    assert(acquire_sem(sem) == B_OK);
    // Already acquired, next with 5ms timeout should fail
    assert(acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, 5000) == B_TIMED_OUT);

    assert(release_sem(sem) == B_OK);
    assert(acquire_sem(sem) == B_OK);
    assert(release_sem(sem) == B_OK);

    assert(delete_sem(sem) == B_OK);
    printf("       create_sem, acquire_sem, timeout, release_sem: PASS\n");
}

struct ThreadContext {
    bool executed;
    int32 received_code;
    char received_buf[64];
};

static status_t worker_thread(void* data)
{
    ThreadContext* ctx = (ThreadContext*)data;
    ctx->executed = true;

    thread_id sender = 0;
    ctx->received_code = receive_data(&sender, ctx->received_buf, sizeof(ctx->received_buf));
    return B_OK;
}

struct LockTestContext {
    rw_lock lock;
    int32 once = INIT_ONCE_UNINITIALIZED;
    sem_id start;
    std::atomic<int> calls{0}, ready{0}, entered{0}, readers{0}, writers{0};
    int initialized = 0;
    int value = 0;
};

static status_t initialize_lock_test(void* data)
{
    auto* context = static_cast<LockTestContext*>(data);
    ++context->calls;
    snooze(20000); // Make simultaneous initialization attempts overlap.
    context->initialized = 42;
    return B_OK;
}

struct LockWorker {
    LockTestContext* context;
    bool writer;
};

static void* contend_lock(void* data)
{
    auto* worker = static_cast<LockWorker*>(data);
    auto& context = *worker->context;
    assert(acquire_sem(context.start) == B_OK);
    assert(__init_once(&context.once, initialize_lock_test, &context) == B_OK);
    assert(context.initialized == 42);
    ++context.ready;
    for (int i = 0; i < 64; ++i) {
        if (worker->writer) {
            assert(rw_lock_write_lock(&context.lock) == B_OK);
            ++context.entered;
            assert(context.writers.fetch_add(1) == 0);
            assert(context.readers.load() == 0);
            ++context.value;
            snooze(10);
            assert(context.writers.fetch_sub(1) == 1);
            assert(rw_lock_write_unlock(&context.lock) == B_OK);
        } else {
            assert(rw_lock_read_lock(&context.lock) == B_OK);
            ++context.entered;
            ++context.readers;
            assert(context.writers.load() == 0);
            assert(context.value >= 0 && context.value <= 128);
            snooze(10);
            --context.readers;
            assert(rw_lock_read_unlock(&context.lock) == B_OK);
        }
    }
    return nullptr;
}

static void test_lock_contention()
{
    LockTestContext context;
    rw_lock_init(&context.lock, "contention_test");
    context.start = create_sem(0, "lock_test_start");
    assert(context.start >= 0);
    assert(rw_lock_write_lock(&context.lock) == B_OK);
    assert(rw_lock_write_lock(&context.lock) == B_OK);
    assert(rw_lock_read_lock(&context.lock) == B_OK);
    pthread_t threads[4];
    LockWorker workers[4];
    for (int i = 0; i < 4; ++i) {
        workers[i] = {&context, i % 2 == 0};
        assert(pthread_create(&threads[i], nullptr, contend_lock, &workers[i]) == 0);
    }
    assert(release_sem_etc(context.start, 4, 0) == B_OK);
    const bigtime_t deadline = system_time() + 5000000;
    while (context.ready.load() != 4 && system_time() < deadline)
        snooze(1000);
    assert(context.ready.load() == 4);
    snooze(5000);
    assert(context.entered.load() == 0); // Every contender must still be blocked.
    assert(context.calls.load() == 1);
    assert(rw_lock_read_unlock(&context.lock) == B_OK);
    assert(rw_lock_write_unlock(&context.lock) == B_OK);
    assert(context.entered.load() == 0); // One recursive write lock remains.
    assert(rw_lock_write_unlock(&context.lock) == B_OK);
    for (auto thread : threads)
        assert(pthread_join(thread, nullptr) == 0);
    assert(context.value == 128);
    assert(rw_lock_write_unlock(&context.lock) == B_ERROR);
    assert(delete_sem(context.start) == B_OK);
    rw_lock_destroy(&context.lock);
    printf("       contended readers/writers, recursive writer, single initialization: PASS\n");
}

static void test_threads(void)
{
    printf("[TEST] threads...\n");
    ThreadContext ctx = {false, 0, {0}};
    thread_id th = spawn_thread(worker_thread, "test_worker", B_NORMAL_PRIORITY, &ctx);
    assert(th > 0);

    // Verify it doesn't execute while suspended
    snooze(10000);
    assert(ctx.executed == false);

    assert(resume_thread(th) == B_OK);
    snooze(10000);
    assert(ctx.executed == true);

    const char* ping = "haiku_mailbox";
    assert(send_data(th, 999, ping, strlen(ping) + 1) == B_OK);

    status_t ret = B_ERROR;
    assert(wait_for_thread(th, &ret) == B_OK);
    assert(ret == B_OK);
    assert(ctx.received_code == 999);
    assert(strcmp(ctx.received_buf, ping) == 0);
    test_lock_contention();
    printf("       spawn_thread, suspended start, resume_thread, send/receive_data, wait_for_thread: PASS\n");
}

static void test_find_directory(void)
{
    printf("[TEST] find_directory...\n");
    char path[PATH_MAX];
    assert(find_directory(B_SYSTEM_DIRECTORY, 0, false, path, sizeof(path)) == B_OK);
    assert(strcmp(path, "/boot/system") == 0);

    assert(find_directory(B_SYSTEM_FONTS_DIRECTORY, 0, false, path, sizeof(path)) == B_OK);
    assert(strcmp(path, "/boot/system/data/fonts") == 0);
    printf("       find_directory B_SYSTEM_DIRECTORY, B_SYSTEM_FONTS_DIRECTORY: PASS\n");
}

int main(int argc, char** argv)
{
    printf("=========================================\n");
    printf("=== Haiku-on-Linux libroot Test Suite ===\n");
    printf("=========================================\n");

    test_system_info();
    test_areas();
    test_ports();
    test_sems();
    test_threads();
    test_find_directory();

    printf("=========================================\n");
    printf("=== ALL 6 HAIKU LIBROOT SUITES PASSED ===\n");
    printf("=========================================\n");
    return 0;
}
