#include <assert.h>
#include <bits/pthreadtypes.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#define RED "\x1b[31m"
#define GREEN "\x1b[32m"
#define BLUE "\x1b[34m"
#define RESET "\x1b[39m"
const char *colors[] = {RED, BLUE, GREEN};

#define TRACE(color, fmt, ...)                                                 \
  do {                                                                         \
    printf("%s" fmt RESET, color, ##__VA_ARGS__);                              \
  } while (0)
#define ERR_TRACE(color, fmt, ...)                                             \
  do {                                                                         \
    fprintf(stderr, "%s" fmt RESET, color, ##__VA_ARGS__);                     \
  } while (0)

int futex_wake_private(atomic_int *uaddr, int n) {
  return syscall(SYS_futex, uaddr, FUTEX_WAKE_PRIVATE, n, NULL, NULL, 0);
}

int futex_wait_private(atomic_int *uaddr, int val) {
  return syscall(SYS_futex, uaddr, FUTEX_WAIT_PRIVATE, val, NULL, NULL, 0);
}

struct mutex {
  atomic_int locked;  // 0 = unlocked, 1 = locked
};

void mutex_init(struct mutex *mtx) { mtx->locked = 0; }

int mutex_lock(struct mutex *mtx) {
  while (1) {
    int expected = 0;

    if (atomic_compare_exchange_weak(&mtx->locked, &expected, 1)) {
      return 0; // ロック取得成功
    }

    if (expected == 1) {
      futex_wait_private(&mtx->locked, expected);
    }
  }
}

int mutex_unlock(struct mutex *mtx) {
  int expected = 1;

  if (atomic_compare_exchange_weak(&mtx->locked, &expected, 0)) {
    futex_wake_private(&mtx->locked, 1);
    return 0; // ロック開放成功
  }

  return -1;
}

struct thread_shared_memory {
  struct mutex mtx;
  atomic_int thread_stop;
};
struct thread_main_arg {
  int32_t thread_id;
  struct thread_shared_memory *shared;
};

void *thread_main(void *arg) {
  struct thread_main_arg *data = arg;
  int thread_id = data->thread_id;
  struct thread_shared_memory *shared = data->shared;

  int counter = 0;

  while (!atomic_load(&shared->thread_stop)) {
    usleep(rand() % (1000 * 100));

    mutex_lock(&shared->mtx);

    TRACE(colors[thread_id], "mutex lock... \n");
    TRACE(colors[thread_id], "counter = %d\n", counter++);
    TRACE(colors[thread_id], "mutex unlock... \n");

    mutex_unlock(&shared->mtx);
  }

  free(arg);

  TRACE(colors[thread_id], "Exit \n");
  return NULL;
}

#define N 3
int main(int argc, const char **argv) {
  int status;
  pthread_t threads[N];

  struct thread_shared_memory *shared =
      malloc(sizeof(struct thread_shared_memory));
  shared->thread_stop = 0;
  mutex_init(&shared->mtx);

  for (int i = 0; i < N; i++) {
    void *arg = ({
      struct thread_main_arg *arg = malloc(sizeof(struct thread_main_arg));
      arg->thread_id = i;
      arg->shared = shared;
      arg;
    });

    status = pthread_create(&threads[i], NULL, thread_main, arg);
    if (status < 0) {
      perror("pthread_create");
      exit(EXIT_FAILURE);
    }
  }

  sleep(1);

  atomic_fetch_add(&shared->thread_stop, 1);

  for (int i = 0; i < N; ++i) {
    pthread_join(threads[i], NULL);
  }
}