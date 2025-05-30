#define _GNU_SOURCE
#include <errno.h>
#include <linux/futex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RED "\x1b[31m"
#define RESET "\x1b[39m"

#define TRACE(color, fmt, ...)                                                        \
  do {                                                                         \
    printf("%s" fmt RESET, color, ##__VA_ARGS__);   \
  } while (0)
#define ERR_TRACE(color, fmt, ...)                                                    \
  do {                                                                         \
    fprintf(stderr, "%s" fmt RESET, color,          \
            ##__VA_ARGS__);                                                    \
  } while (0)

int sys_futex(volatile int *uaddr, int op, int val) {
  return syscall(SYS_futex, uaddr, op, val, NULL, NULL, 0);
}

struct futex_data {
  volatile int32_t wake_futex;
  volatile int32_t counter;
  volatile time_t timestamp;
  volatile int32_t random;
  volatile uint32_t quit_flag;
} __attribute__((aligned(64)));

int main(int argc, const char **argv) {
  int status;
  int pid;

  // 共有メモリを作成
  struct futex_data *futex_struct =
      mmap(NULL, sizeof(struct futex_data), PROT_READ | PROT_WRITE,
           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (futex_struct == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  // 初期化
  futex_struct->wake_futex = 0;
  futex_struct->counter = 0;
  futex_struct->timestamp = 0;
  futex_struct->random = 0;
  futex_struct->quit_flag = 0;

  pid = fork();
  if (pid == 0) {
    int32_t expected_value = futex_struct->wake_futex;
    TRACE(RED, "Hi♡\n");

    while (futex_struct->quit_flag == 0) {

      TRACE(RED, "Wait...\n");

      if (sys_futex(&futex_struct->wake_futex, FUTEX_WAIT,
                    expected_value) < 0) {
        ERR_TRACE(RED, "%s\n", strerror(errno));
      }

      TRACE(RED, "Woke up!\n");
      TRACE(RED, "counter = %d\n", futex_struct->counter);
      TRACE(RED, "timestamp = %lu\n", futex_struct->timestamp);
      TRACE(RED, "random = %d\n", futex_struct->random);

      expected_value =
          __atomic_load_n(&futex_struct->wake_futex, __ATOMIC_RELAXED);
    }

    TRACE(RED, "Child process exiting\n");
    exit(0);
  } else {
    for (int i = 0; i < 10; ++i) {

      int random_time_us = rand() % (1000 * 1000);
      usleep(random_time_us);

      time_t timestamp = time(NULL);

      __atomic_fetch_add(&futex_struct->counter, 1, __ATOMIC_SEQ_CST);
      __atomic_store(&futex_struct->timestamp, &timestamp, __ATOMIC_SEQ_CST);
      __atomic_store(&futex_struct->random, &random_time_us, __ATOMIC_SEQ_CST);
      __atomic_fetch_add(&futex_struct->wake_futex, 1, __ATOMIC_SEQ_CST);

      TRACE(RESET, "Wake up thread\n");
      int woken = sys_futex(&futex_struct->wake_futex, FUTEX_WAKE, 1);
      if (woken < 0) {
        ERR_TRACE(RESET, "futex_wake failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
      } else if (woken == 0) {
        TRACE(RESET, "No threads were waiting\n");
      }
    }

    __atomic_store_n(&futex_struct->quit_flag, 1, __ATOMIC_SEQ_CST);
    __atomic_fetch_add(&futex_struct->wake_futex, 1, __ATOMIC_SEQ_CST);
    sys_futex(&futex_struct->wake_futex, FUTEX_WAKE, 1);

    // 子プロセスの終了を待機
    wait(&status);
    TRACE(RESET, "Parent process exiting\n");
  }

  munmap(futex_struct, sizeof(struct futex_data));
  return 0;
}