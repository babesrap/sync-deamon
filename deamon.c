#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <sys/mman.h>
#include <utime.h>
#include <time.h>

#define DEFAULT_INTERVAL 300  // 5 minutes
static volatile sig_atomic_t wakeup_flag = 0;
static int recursive = 0;
static size_t threshold = 0;  // 0 = default only read/write
static int interval_secs = DEFAULT_INTERVAL;
static const char *src_root;
static const char *dst_root;
static volatile sig_atomic_t exit_flag = 0;

void handle_sigusr1(int signo) {
    wakeup_flag = 1;
}

void handle_sigterm(int signo) {
    exit_flag = 1;
}

void daemonize(void) {
    pid_t pid;
    if ((pid = fork()) < 0) exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);
    if (setsid() < 0) exit(EXIT_FAILURE);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    if ((pid = fork()) < 0) exit(EXIT_FAILURE);
    if (pid > 0) _exit(EXIT_SUCCESS);
    umask(0);
    chdir("/");
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

int is_directory(const char *path) {
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

void copy_file(const char *src, const char *dst, const struct stat *st) {
    syslog(LOG_INFO, "Copying %s -> %s", src, dst);
    int infd = open(src, O_RDONLY);
    if (infd < 0) { syslog(LOG_ERR, "open src failed %s: %m", src); return; }
    int outfd = open(dst, O_RDWR | O_CREAT | O_TRUNC, st->st_mode & 0666);
    if (outfd < 0) { syslog(LOG_ERR, "open dst failed %s: %m", dst); close(infd); return; }
    if (threshold > 0 && (size_t)st->st_size >= threshold) {
        void *map = mmap(NULL, st->st_size, PROT_READ, MAP_SHARED, infd, 0);
        if (map == MAP_FAILED) {
            syslog(LOG_ERR, "mmap failed for %s: %m", src);
        } else {
            if (write(outfd, map, st->st_size) < 0)
                syslog(LOG_ERR, "write mmap data failed: %m");
            munmap(map, st->st_size);
        }
    } else {
        char buf[8192];
        ssize_t r;
        while ((r = read(infd, buf, sizeof(buf))) > 0) {
            if (write(outfd, buf, r) != r) {
                syslog(LOG_ERR, "write failed: %m");
                break;
            }
        }
    }
    struct utimbuf times = { .actime = st->st_atime, .modtime = st->st_mtime };
    utime(dst, &times);
    close(infd);
    close(outfd);
}

void remove_dir_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    char child[PATH_MAX];
    struct stat st;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (lstat(child, &st)==0 && S_ISDIR(st.st_mode)) {
            remove_dir_recursive(child);
            rmdir(child);
            syslog(LOG_INFO, "Removed directory %s", child);
        } else {
            unlink(child);
            syslog(LOG_INFO, "Removed file %s", child);
        }
    }
    closedir(d);
}

void sync_tree(const char *src, const char *dst) {
    DIR *d = opendir(src);
    if (!d) { syslog(LOG_ERR, "opendir src failed %s: %m", src); return; }
    struct dirent *e;
    char src_path[PATH_MAX], dst_path[PATH_MAX];
    struct stat st_src, st_dst;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0) continue;
        snprintf(src_path, sizeof(src_path), "%s/%s", src, e->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, e->d_name);
        if (lstat(src_path, &st_src)<0) continue;
        if (S_ISREG(st_src.st_mode)) {
            int need = 1;
            if (stat(dst_path, &st_dst)==0 && S_ISREG(st_dst.st_mode)) {
                need = (st_src.st_mtime > st_dst.st_mtime);
            }
            if (need) copy_file(src_path, dst_path, &st_src);
        } else if (recursive && S_ISDIR(st_src.st_mode)) {
            if (stat(dst_path, &st_dst)<0) {
                mkdir(dst_path, st_src.st_mode & 0777);
                syslog(LOG_INFO, "Created directory %s", dst_path);
            }
            sync_tree(src_path, dst_path);
        }
    }
    closedir(d);
}

void purge_tree(const char *src, const char *dst) {
    DIR *d = opendir(dst);
    if (!d) return;
    struct dirent *e;
    char src_path[PATH_MAX], dst_path[PATH_MAX];
    struct stat st;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0) continue;
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, e->d_name);
        snprintf(src_path, sizeof(src_path), "%s/%s", src, e->d_name);
        if (lstat(src_path, &st)<0) {
            // not in src
            if (lstat(dst_path, &st)==0 && S_ISDIR(st.st_mode) && recursive) {
                remove_dir_recursive(dst_path);
                rmdir(dst_path);
                syslog(LOG_INFO, "Removed directory %s", dst_path);
            } else {
                unlink(dst_path);
                syslog(LOG_INFO, "Removed file %s", dst_path);
            }
        } else if (recursive && S_ISDIR(st.st_mode)) {
            purge_tree(src_path, dst_path);
        }
    }
    closedir(d);
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "Rs:t:")) != -1) {
        switch (opt) {
            case 'R': recursive = 1; break;
            case 's': interval_secs = atoi(optarg); break;
            case 't': threshold = strtoull(optarg, NULL, 10); break;
            default:
                fprintf(stderr, "Usage: %s [-R] [-s interval] [-t threshold] <src> <dst>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (optind + 2 > argc) {
        fprintf(stderr, "Need source and destination directories\n");
        exit(EXIT_FAILURE);
    }
    src_root = argv[optind];
    dst_root = argv[optind+1];
    // convert to absolute paths so daemon chdir("/") won't break relative names
    {
        char abs_src[PATH_MAX], abs_dst[PATH_MAX];
        if (realpath(src_root, abs_src) == NULL || !is_directory(abs_src)) {
            fprintf(stderr, "Invalid source path after realpath: %s\n", src_root);
            exit(EXIT_FAILURE);
        }
        if (realpath(dst_root, abs_dst) == NULL || !is_directory(abs_dst)) {
            fprintf(stderr, "Invalid destination path after realpath: %s\n", dst_root);
            exit(EXIT_FAILURE);
        }
        src_root = strdup(abs_src);
        dst_root = strdup(abs_dst);
    }
    openlog("dirsyncd", LOG_PID, LOG_DAEMON);
    signal(SIGTERM, handle_sigterm);
    signal(SIGUSR1, handle_sigusr1);
    daemonize();
    // re-install handlers after daemonize to be safe
    signal(SIGTERM, handle_sigterm);
    signal(SIGUSR1, handle_sigusr1);
    while (!exit_flag) {
        syslog(LOG_INFO, "Daemon sleeping for %d seconds", interval_secs);
        wakeup_flag = 0;
        int rem = interval_secs;
        while (rem > 0 && !wakeup_flag)
        {
            if(exit_flag) goto EXIT;
            rem = sleep(rem);
        }
        syslog(LOG_INFO, "Daemon woke up %s", wakeup_flag ? "by signal" : "naturally");
        sync_tree(src_root, dst_root);
        purge_tree(src_root, dst_root);
    }

EXIT:
    syslog(LOG_INFO, "Daemon shutting down");
    closelog();
    return 0;
}