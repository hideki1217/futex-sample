#include <assert.h>
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

struct task {
  uint task_id;
  void *func_arg;
  void (*func)(int, void *);
};

#define QUEUE_SIZE 128
struct queue {
  struct task tasks[QUEUE_SIZE];
  atomic_int alive;
  atomic_uint head;
  atomic_uint tail;
  atomic_int producer;
  atomic_int consumer;
  atomic_int joiner;
};

void queue_init(struct queue *q) {
  q->alive = 1;
  q->head = 0;
  q->tail = 0;
  q->producer = 0;
  q->consumer = 0;
  q->joiner = 0;
  memset(q->tasks, 0, sizeof(q->tasks));
}

int queue_push(struct queue *q, const struct task task) {
  while (atomic_load(&q->alive)) {
    uint head = atomic_load(&q->head);
    uint tail = atomic_load(&q->tail);
    if (tail - head < QUEUE_SIZE) {
      if (atomic_compare_exchange_weak(&q->tail, &tail, tail + 1)) {
        q->tasks[tail % QUEUE_SIZE] = task;

        atomic_fetch_add(&q->consumer, 1);
        futex_wake_private(&q->consumer, 1);
        return 1;
      }
    } else {
      // Full
      int current = atomic_load(&q->producer);
      futex_wait_private(&q->producer, current);
    }
  }

  return 0;
}

int queue_pop(struct queue *q, struct task *task) {
  while (atomic_load(&q->alive)) {
    uint head = atomic_load(&q->head);
    uint tail = atomic_load(&q->tail);
    if (head < tail) {
      if (atomic_compare_exchange_weak(&q->head, &head, head + 1)) {
        memcpy(task, &q->tasks[head % QUEUE_SIZE], sizeof(struct task));

        atomic_fetch_add(&q->producer, 1);
        futex_wake_private(&q->producer, 1);
        return 1;
      }
    } else {
      // Empty
      atomic_fetch_add(&q->joiner, 1);
      futex_wake_private(&q->joiner, INT32_MAX);

      int current = atomic_load(&q->consumer);
      futex_wait_private(&q->consumer, current);
    }
  }

  return 0;
}

int queue_join(struct queue *q) {
  while (1) {
    uint head = atomic_load(&q->head);
    uint tail = atomic_load(&q->tail);
    if (head < tail) {
      int current = atomic_load(&q->joiner);
      futex_wait_private(&q->joiner, current);
    } else {
      return 1;
    }
  }
}

int queue_kill(struct queue *q) {
  atomic_store(&q->alive, 0);

  atomic_fetch_add(&q->consumer, 1);
  atomic_fetch_add(&q->producer, 1);
  futex_wake_private(&q->consumer, INT32_MAX);
  futex_wake_private(&q->producer, INT32_MAX);
}

struct queue q;

#define PRODUCER_NUM 2
#define CONSUMER_NUM 3
const char *colors[CONSUMER_NUM] = {RED, BLUE, GREEN};

atomic_int stop_flag = 0;
static void *consumer_main(void *arg) {
  int thread_id = (long)arg;
  struct task task;

  TRACE(colors[thread_id], "Thread %d start...\n", thread_id);

  while (!atomic_load(&stop_flag)) {
    if (queue_pop(&q, &task)) {
      task.func(thread_id, task.func_arg);
    }
  }

  TRACE(colors[thread_id], "Thread %d stop\n", thread_id);

  return NULL;
}

static void task_func(int thread_id, void *arg) {
  int task_id = (long)arg;

  TRACE(colors[thread_id], "(task_id: %d) > > >\n", task_id);

  usleep(rand() % (1000 * 100));

  TRACE(colors[thread_id], "< < < (task_id: %d)\n", task_id);
}

static void *producer_main(void *arg) {
  int thread_id = (long)arg;
  struct task task;

  TRACE(RESET, "Producer %d start...\n", thread_id);

  for (int i = 0; i < 256; ++i) {
    int task_id = i + (1 << (thread_id * 10));
    struct task task = {
        .task_id = task_id,
        .func_arg = (void *)(long)task_id,
        .func = task_func,
    };
    queue_push(&q, task);

    usleep(rand() % (1000 * 10));
  }

  TRACE(RESET, "Producer %d stop\n", thread_id);

  return NULL;
}

int main(int argc, const char **argv) {
  int status;
  pthread_t consumers[CONSUMER_NUM];
  pthread_t producers[PRODUCER_NUM];

  queue_init(&q);

  for (int i = 0; i < PRODUCER_NUM; ++i) {
    pthread_create(&producers[i], NULL, producer_main, (void *)(long)i);
  }

  for (int i = 0; i < CONSUMER_NUM; ++i) {
    pthread_create(&consumers[i], NULL, consumer_main, (void *)(long)i);
  }

  for (int i = 0; i < PRODUCER_NUM; i++) {
    pthread_join(producers[i], NULL);
  }

  queue_join(&q);
  atomic_store(&stop_flag, 1);
  queue_kill(&q);

  for (int i = 0; i < CONSUMER_NUM; i++) {
    pthread_join(consumers[i], NULL);
  }

  printf("All done!\n");
  return 0;
}