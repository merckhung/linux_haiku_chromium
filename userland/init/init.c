/*
 * Copyright 2026, linux_haiku_chromium contributors.
 * Distributed under the terms of the MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>

static void mount_essential_filesystems(void)
{
    mkdir("/proc", 0755);
    if (mount("proc", "/proc", "proc", 0, NULL) < 0 && errno != EBUSY) {
        perror("init: mount /proc");
    }

    mkdir("/sys", 0755);
    if (mount("sysfs", "/sys", "sysfs", 0, NULL) < 0 && errno != EBUSY) {
        perror("init: mount /sys");
    }

    mkdir("/dev", 0755);
    if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL) < 0 && errno != EBUSY) {
        perror("init: mount /dev");
    }

    mkdir("/dev/pts", 0755);
    if (mount("devpts", "/dev/pts", "devpts", 0, "ptmxmode=0666,mode=0620") < 0 && errno != EBUSY) {
        if (mount("devpts", "/dev/pts", "devpts", 0, NULL) < 0 && errno != EBUSY) {
            perror("init: mount /dev/pts");
        }
    }
    chmod("/dev/ptmx", 0666);

    mkdir("/dev/shm", 1777);
    if (mount("tmpfs", "/dev/shm", "tmpfs", 0, "mode=1777") < 0 && errno != EBUSY) {
        perror("init: mount /dev/shm");
    }

    mkdir("/tmp", 1777);
    if (mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777") < 0 && errno != EBUSY) {
        perror("init: mount /tmp");
    }

    mkdir("/run", 0755);
    if (mount("tmpfs", "/run", "tmpfs", 0, "mode=0755") < 0 && errno != EBUSY) {
        perror("init: mount /run");
    }

    FILE *f_overcommit = fopen("/proc/sys/vm/overcommit_memory", "w");
    if (f_overcommit) {
        fputs("1\n", f_overcommit);
        fclose(f_overcommit);
    }
    FILE *f_map = fopen("/proc/sys/vm/max_map_count", "w");
    if (f_map) {
        fputs("262144\n", f_map);
        fclose(f_map);
    }
}

static void setup_environment(void)
{
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/sbin:/boot/system/bin:/boot/system/apps", 1);
    setenv("LD_LIBRARY_PATH", "/lib:/usr/lib:/boot/system/lib:/boot/system/apps", 1);
    setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1);
    setenv("FONTCONFIG_PATH", "/etc/fonts", 1);
    setenv("HOME", "/boot/home", 1);
    setenv("USER", "root", 1);
    setenv("SHELL", "/bin/sh", 1);
    setenv("TERM", "linux", 1);
}

static void ensure_directories(void)
{
    mkdir("/etc", 0755);
    if (access("/etc/passwd", F_OK) != 0) {
        FILE *f = fopen("/etc/passwd", "w");
        if (f) {
            fprintf(f, "root:x:0:0:root:/boot/home:/bin/sh\n");
            fclose(f);
        }
    }
    if (access("/etc/group", F_OK) != 0) {
        FILE *f = fopen("/etc/group", "w");
        if (f) {
            fprintf(f, "root:x:0:\ntty:x:5:\n");
            fclose(f);
        }
    }
    if (access("/etc/hosts", F_OK) != 0) {
        FILE *f = fopen("/etc/hosts", "w");
        if (f) {
            fprintf(f, "127.0.0.1 localhost\n::1 localhost\n");
            fclose(f);
        }
    }
    if (access("/etc/resolv.conf", F_OK) != 0) {
        FILE *f = fopen("/etc/resolv.conf", "w");
        if (f) {
            fprintf(f, "nameserver 10.0.2.3\nnameserver 8.8.8.8\n");
            fclose(f);
        }
    }
    if (access("/etc/nsswitch.conf", F_OK) != 0) {
        FILE *f = fopen("/etc/nsswitch.conf", "w");
        if (f) {
            fprintf(f, "hosts: files dns\nnetworks: files\n");
            fclose(f);
        }
    }
    mkdir("/etc/ssl", 0755);
    mkdir("/etc/ssl/certs", 0755);
    mkdir("/etc/fonts", 0755);
    mkdir("/tmp/fontconfig", 0777);
    if (access("/etc/fonts/fonts.conf", F_OK) != 0) {
        FILE *f = fopen("/etc/fonts/fonts.conf", "w");
        if (f) {
            fprintf(f,
                "<?xml version=\"1.0\"?>\n"
                "<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
                "<fontconfig>\n"
                "  <dir>/boot/system/data/fonts</dir>\n"
                "  <cachedir>/tmp/fontconfig</cachedir>\n"
                "</fontconfig>\n");
            fclose(f);
        }
    }
    mkdir("/boot", 0755);
    mkdir("/boot/home", 0755);
    mkdir("/boot/home/Desktop", 0755);
    mkdir("/boot/home/config", 0755);
    mkdir("/boot/home/config/settings", 0755);
    mkdir("/boot/system", 0755);
    mkdir("/boot/system/bin", 0755);
    mkdir("/boot/system/lib", 0755);
    mkdir("/boot/system/data", 0755);
    mkdir("/boot/system/data/fonts", 0755);
    mkdir("/boot/system/data/artwork", 0755);
    mkdir("/boot/system/servers", 0755);
    mkdir("/boot/system/apps", 0755);
    mkdir("/tmp/chrome-profile", 0755);
    mkdir("/tmp/chrome-profile/Default", 0755);
    if (access("/tmp/chrome-profile/Default/Preferences", F_OK) != 0) {
        FILE *f = fopen("/tmp/chrome-profile/Default/Preferences", "w");
        if (f) {
            fprintf(f,
                "{\"homepage\":\"https://www.yahoo.com\","
                "\"homepage_is_newtabpage\":false,"
                "\"browser\":{\"show_home_button\":true},"
                "\"session\":{\"restore_on_startup\":4,"
                "\"startup_urls\":[\"https://www.yahoo.com\"]}}\n");
            fclose(f);
        }
    }
    if (access("/boot/system/bin/chromium", F_OK) != 0) {
        FILE *f = fopen("/boot/system/bin/chromium", "w");
        if (f) {
            fprintf(f,
                "#!/bin/sh\n"
                "URL=\"${1:-https://www.yahoo.com}\"\n"
                "exec /boot/system/apps/chrome \\\n"
                "  --no-sandbox --no-zygote --test-type --no-first-run \\\n"
                "  --no-default-browser-check --disable-dev-shm-usage \\\n"
                "  --disable-gpu --in-process-gpu --ozone-platform=haiku \\\n"
                "  --password-store=basic \\\n"
                "  --renderer-process-limit=2 --process-per-site \\\n"
                "  --disable-site-isolation-trials \\\n"
                "  --disable-features=Translate,OptimizationGuideModelDownloading,OnDeviceModelExecution,MediaRouter,DialMediaRouteProvider \\\n"
                "  --js-flags=--max-old-space-size=512 \\\n"
                "  --disable-sync --disable-extensions --disable-component-update \\\n"
                "  --disable-background-networking --disable-notifications \\\n"
                "  --user-data-dir=/tmp/chrome-profile \\\n"
                "  --window-size=780,560 --window-position=470,120 \"$URL\"\n");
            fclose(f);
            chmod("/boot/system/bin/chromium", 0755);
        }
    }
}

static int run_command_wait(const char *cmd, char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execvp(cmd, argv);
        perror(cmd);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            return -1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return -1;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    // Ensure we are PID 1
    if (getpid() != 1) {
        fprintf(stderr, "Warning: Not running as PID 1\n");
    }

    // Redirect standard I/O to console if needed
    int fd = open("/dev/console", O_RDWR);
    if (fd >= 0) {
        if (dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) {
            // failed to redirect
        }
        if (fd > 2)
            close(fd);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }

    printf("\n");
    printf("=================================================================\n");
#if defined(__aarch64__)
    printf("=== Linux + Haiku GUI + Chromium Embedded System (64-bit ARM64) ===\n");
#else
    printf("=== Linux + Haiku GUI + Chromium Embedded System (64-bit x86_64) ===\n");
#endif
    printf("=================================================================\n\n");

    mount_essential_filesystems();
    setup_environment();
    ensure_directories();

    if (access("/bin/ifconfig", X_OK) == 0) {
        char *lo_argv[] = {"/bin/ifconfig", "lo", "127.0.0.1", "up", NULL};
        run_command_wait("/bin/ifconfig", lo_argv);

        char *eth_argv[] = {"/bin/ifconfig", "eth0", "10.0.2.15", "netmask", "255.255.255.0", "up", NULL};
        if (run_command_wait("/bin/ifconfig", eth_argv) == 0) {
            if (access("/bin/route", X_OK) == 0) {
                char *gw_argv[] = {"/bin/route", "add", "default", "gw", "10.0.2.2", NULL};
                run_command_wait("/bin/route", gw_argv);
            } else if (access("/bin/ip", X_OK) == 0) {
                char *ip_argv[] = {"/bin/ip", "route", "add", "default", "via", "10.0.2.2", "dev", "eth0", NULL};
                run_command_wait("/bin/ip", ip_argv);
            }
            printf("[init] Network eth0 configured: 10.0.2.15/24 gw 10.0.2.2 dns 10.0.2.3\n");
        }
    }

    // Check /proc/cmdline for run mode
    char cmdline[1024] = {0};
    int cl_fd = open("/proc/cmdline", O_RDONLY);
    if (cl_fd >= 0) {
        read(cl_fd, cmdline, sizeof(cmdline) - 1);
        close(cl_fd);
    }

    printf("[init] Kernel command line: %s\n", cmdline);

    // Mode selection:
    // 1. "haiku.mode=tier1" or default: run tests and open shell
    // 2. "haiku.mode=gui" / "haiku.mode=tier2": launch app_server + Deskbar + Terminal
    // 3. "haiku.mode=chromium" / "haiku.mode=tier3": launch app_server + Chromium
    bool test_mode = (strstr(cmdline, "haiku.mode=tier1") != NULL) || (strstr(cmdline, "haiku.test=1") != NULL);
    bool gui_mode = (strstr(cmdline, "haiku.mode=gui") != NULL) || (strstr(cmdline, "haiku.mode=tier2") != NULL);
    bool chromium_mode = (strstr(cmdline, "haiku.mode=chromium") != NULL) || (strstr(cmdline, "haiku.mode=tier3") != NULL);

    if (test_mode || (!gui_mode && !chromium_mode)) {
        printf("[init] Executing Haiku libroot verification suite...\n");
        char *test_argv[] = {"/bin/haiku_libroot_test", NULL};
        int rc = run_command_wait("/bin/haiku_libroot_test", test_argv);
        printf("[init] haiku_libroot_test exited with code: %d\n", rc);
        if (rc == 0) {
            printf("[init] [PASS] Tier 1 verification passed!\n");
        } else {
            printf("[init] [FAIL] Tier 1 verification failed!\n");
        }
    }

    pid_t db_pid = -1;
    if (gui_mode || chromium_mode) {
        printf("[init] Launching Haiku GUI subsystem...\n");
        // Start app_server in background
        pid_t app_pid = fork();
        if (app_pid == 0) {
            char *app_argv[] = {"/boot/system/servers/app_server", NULL};
            execv(app_argv[0], app_argv);
            perror("app_server");
            _exit(1);
        }
        printf("[init] Forked app_server with PID %d\n", app_pid);

        // Wait until app_server reports that its server port and Desktop are
        // up (it writes /run/app_server.ready). Emulated ARM64 (QEMU TCG) can
        // take many seconds to get there, so poll instead of a fixed sleep.
        {
            int waited_ms = 0;
            while (access("/run/app_server.ready", F_OK) != 0 && waited_ms < 120000) {
                usleep(100000);
                waited_ms += 100;
            }
            if (access("/run/app_server.ready", F_OK) == 0) {
                printf("[init] app_server ready after %d ms\n", waited_ms);
            } else {
                printf("[init] WARNING: app_server not ready after %d ms; continuing\n", waited_ms);
            }
            usleep(300000);
        }

        // Start registrar in background if present
        if (access("/boot/system/servers/registrar", X_OK) == 0) {
            pid_t reg_pid = fork();
            if (reg_pid == 0) {
                char *reg_argv[] = {"/boot/system/servers/registrar", NULL};
                execv(reg_argv[0], reg_argv);
                perror("registrar");
                _exit(1);
            }
            printf("[init] Forked registrar with PID %d\n", reg_pid);
            usleep(200000);
        }

        if (chromium_mode) {
            // First launch Deskbar (Taskbar) on the graphical home screen
            if (access("/boot/system/apps/Deskbar", X_OK) == 0) {
                printf("[init] Launching Haiku Deskbar (Taskbar)...\n");
                db_pid = fork();
                if (db_pid == 0) {
                    char *db_argv[] = {"/boot/system/apps/Deskbar", NULL};
                    execv(db_argv[0], db_argv);
                    perror("Deskbar");
                    _exit(1);
                }
                printf("[init] Forked Deskbar with PID %d\n", db_pid);
                usleep(350000);
            }

            if (access("/boot/system/apps/Terminal", X_OK) == 0) {
                printf("[init] Launching Haiku Terminal...\n");
                pid_t term_pid = fork();
                if (term_pid == 0) {
                    char *term_argv[] = {"/boot/system/apps/Terminal", NULL};
                    execv(term_argv[0], term_argv);
                    perror("Terminal");
                    _exit(1);
                }
                printf("[init] Forked Terminal with PID %d\n", term_pid);
                usleep(350000);
            }

            if (access("/boot/system/apps/WidgetGallery", X_OK) == 0) {
                printf("[init] Launching Haiku GUI Widget Gallery...\n");
                pid_t wg_pid = fork();
                if (wg_pid == 0) {
                    char *wg_argv[] = {"/boot/system/apps/WidgetGallery", NULL};
                    execv(wg_argv[0], wg_argv);
                    perror("WidgetGallery");
                    _exit(1);
                }
                printf("[init] Forked WidgetGallery with PID %d\n", wg_pid);
                usleep(500000);
            }

            printf("[init] Launching 64-bit Chromium on Haiku GUI...\n");
            pid_t chrome_pid = fork();
            if (chrome_pid == 0) {
                char *chrome_argv[] = {
                    "/boot/system/apps/chrome",
                    "--no-sandbox",
                    "--no-zygote",
                    "--test-type",
                    "--no-first-run",
                    "--no-default-browser-check",
                    "--disable-dev-shm-usage",
                    "--disable-gpu",
                    "--in-process-gpu",
                    "--ozone-platform=haiku",
                    "--password-store=basic",
                    "--renderer-process-limit=2",
                    "--process-per-site",
                    "--disable-site-isolation-trials",
                    "--disable-features=Translate,OptimizationGuideModelDownloading,OnDeviceModelExecution,MediaRouter,DialMediaRouteProvider",
                    "--js-flags=--max-old-space-size=512",
                    "--disable-sync",
                    "--disable-extensions",
                    "--disable-component-update",
                    "--disable-background-networking",
                    "--disable-notifications",
                    "--user-data-dir=/tmp/chrome-profile",
                    "--window-size=780,560",
                    "--window-position=470,120",
                    "https://www.yahoo.com",
                    NULL
                };
                execv(chrome_argv[0], chrome_argv);
                perror("chrome");
                _exit(1);
            }
            printf("[init] Forked chrome with PID %d\n", chrome_pid);
        } else {
            // Launch Deskbar and Terminal in GUI desktop mode
            if (access("/boot/system/apps/Deskbar", X_OK) == 0) {
                pid_t db_pid = fork();
                if (db_pid == 0) {
                    char *db_argv[] = {"/boot/system/apps/Deskbar", NULL};
                    execv(db_argv[0], db_argv);
                    perror("Deskbar");
                    _exit(1);
                }
                printf("[init] Forked Deskbar with PID %d\n", db_pid);
                usleep(350000);
            }
            if (access("/boot/system/apps/Terminal", X_OK) == 0) {
                pid_t term_pid = fork();
                if (term_pid == 0) {
                    char *term_argv[] = {"/boot/system/apps/Terminal", NULL};
                    execv(term_argv[0], term_argv);
                    perror("Terminal");
                    _exit(1);
                }
                printf("[init] Forked Terminal with PID %d\n", term_pid);
            }
            if (access("/boot/system/apps/WidgetGallery", X_OK) == 0) {
                usleep(800000);
                printf("[init] Launching Haiku GUI Widget Gallery...\n");
                pid_t wg_pid = fork();
                if (wg_pid == 0) {
                    char *wg_argv[] = {"/boot/system/apps/WidgetGallery", NULL};
                    execv(wg_argv[0], wg_argv);
                    perror("WidgetGallery");
                    _exit(1);
                }
                printf("[init] Forked WidgetGallery with PID %d\n", wg_pid);
            }
        }
    }

    // If requested or as fallback, run BusyBox shell
    printf("[init] Starting interactive system shell...\n");
    pid_t shell_pid = fork();
    if (shell_pid == 0) {
        char *sh_argv[] = {"/bin/sh", NULL};
        execv("/bin/sh", sh_argv);
        perror("exec /bin/sh");
        _exit(1);
    }

    // PID 1 main loop: reap zombies and supervise children
    while (1) {
        int status;
        pid_t p = waitpid(-1, &status, 0);
        if (p > 0) {
            if (WIFEXITED(status)) {
                printf("[init] Child process %d exited with status %d\n", p, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                printf("[init] Child process %d terminated by signal %d (%s)\n", p, WTERMSIG(status), strsignal(WTERMSIG(status)));
            }

            if (p == shell_pid) {
                printf("[init] Shell terminated. Respawning...\n");
                sleep(1);
                shell_pid = fork();
                if (shell_pid == 0) {
                    char *sh_argv[] = {"/bin/sh", NULL};
                    execv("/bin/sh", sh_argv);
                    _exit(1);
                }
            } else if (p == db_pid && db_pid > 0) {
                printf("[init] Deskbar terminated. Respawning...\n");
                sleep(1);
                db_pid = fork();
                if (db_pid == 0) {
                    char *db_argv[] = {"/boot/system/apps/Deskbar", NULL};
                    execv(db_argv[0], db_argv);
                    _exit(1);
                }
            }
        } else if (p < 0 && errno != EINTR) {
            sleep(1);
        }
    }

    return 0;
}
