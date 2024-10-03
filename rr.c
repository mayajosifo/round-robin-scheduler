#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef uint32_t u32;
typedef int32_t i32;

struct process
{
  u32 pid;
  u32 arrival_time;
  u32 burst_time;

  TAILQ_ENTRY(process) pointers;

  /* Additional fields here */
  u32 remaining_time;
  u32 start_exec_time;
  /* End of "Additional fields here" */
};

TAILQ_HEAD(process_list, process);

u32 next_int(const char **data, const char *data_end)
{
  u32 current = 0;
  bool started = false;
  while (*data != data_end)
  {
    char c = **data;

    if (c < 0x30 || c > 0x39)
    {
      if (started)
      {
        return current;
      }
    }
    else
    {
      if (!started)
      {
        current = (c - 0x30);
        started = true;
      }
      else
      {
        current *= 10;
        current += (c - 0x30);
      }
    }

    ++(*data);
  }

  printf("Reached end of file while looking for another integer\n");
  exit(EINVAL);
}

u32 next_int_from_c_str(const char *data)
{
  char c;
  u32 i = 0;
  u32 current = 0;
  bool started = false;
  while ((c = data[i++]))
  {
    if (c < 0x30 || c > 0x39)
    {
      exit(EINVAL);
    }
    if (!started)
    {
      current = (c - 0x30);
      started = true;
    }
    else
    {
      current *= 10;
      current += (c - 0x30);
    }
  }
  return current;
}

void init_processes(const char *path,
                    struct process **process_data,
                    u32 *process_size)
{
  int fd = open(path, O_RDONLY);
  if (fd == -1)
  {
    int err = errno;
    perror("open");
    exit(err);
  }

  struct stat st;
  if (fstat(fd, &st) == -1)
  {
    int err = errno;
    perror("stat");
    exit(err);
  }

  u32 size = st.st_size;
  const char *data_start = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (data_start == MAP_FAILED)
  {
    int err = errno;
    perror("mmap");
    exit(err);
  }

  const char *data_end = data_start + size;
  const char *data = data_start;

  *process_size = next_int(&data, data_end);

  *process_data = calloc(sizeof(struct process), *process_size);
  if (*process_data == NULL)
  {
    int err = errno;
    perror("calloc");
    exit(err);
  }

  for (u32 i = 0; i < *process_size; ++i)
  {
    (*process_data)[i].pid = next_int(&data, data_end);
    (*process_data)[i].arrival_time = next_int(&data, data_end);
    (*process_data)[i].burst_time = next_int(&data, data_end);
  }

  munmap((void *)data, size);
  close(fd);
}

// we have to make a comparison operator to use in our sorting of the processes by arrival time
int compare_by_arrival_time(const void *a, const void *b) {
    const struct process *proc1 = (const struct process *)a;
    const struct process *proc2 = (const struct process *)b;

    if (proc1->arrival_time < proc2->arrival_time) return -1;
    if (proc1->arrival_time > proc2->arrival_time) return 1;
    return 0;
}

int main(int argc, char *argv[])
{
  if (argc != 3)
  {
    return EINVAL;
  }
  struct process *data;
  u32 size;
  init_processes(argv[1], &data, &size);

  u32 quantum_length = next_int_from_c_str(argv[2]);

  struct process_list list;
  TAILQ_INIT(&list);

  u32 total_waiting_time = 0;
  u32 total_response_time = 0;

  /* Your code here */

  // basic error check
  if(quantum_length <= 0 || size == 0){return EINVAL;}

  // sort data in case processes are not in order of arrival time
  qsort(data, size, sizeof(struct process), compare_by_arrival_time);

  //populate list with all of the processes + their default extra values
  for (u32 i = 0; i < size; ++i) {
      data[i].start_exec_time = -1;
      data[i].remaining_time = data[i].burst_time;  
      TAILQ_INSERT_TAIL(&list, &data[i], pointers);
  }

  // instantiate ready queue
  struct process_list ready_queue;
  TAILQ_INIT(&ready_queue);

  // figure out what the first arrival time is
  struct process *first_proc = TAILQ_FIRST(&list);  
  u32 earliest_arrival = first_proc->arrival_time;  
  
  // this adds the first process(es) to the ready queue
  // use foreach_safe and a temp value 
  // because we're editing the lists (causes pointer errors otherwise)
  struct process *proc, *temp_proc;
  TAILQ_FOREACH_SAFE(proc, &list, pointers, temp_proc) {
    if (proc->arrival_time == earliest_arrival) {
        TAILQ_REMOVE(&list, proc, pointers);
        TAILQ_INSERT_TAIL(&ready_queue, proc, pointers);
    }
  }

  // start the "clock" of the scheduler
  u32 current_time = earliest_arrival; // the scheduler is idle until the first process arrives

  // run until there's no processes left
  while (!TAILQ_EMPTY(&list) || !TAILQ_EMPTY(&ready_queue)) {

    // the ready queue has no remaining items (ex: when a process finishes and there's a break before another process is queued)
    if (TAILQ_EMPTY(&ready_queue)) {
      struct process *next_process = TAILQ_FIRST(&list), *temp_process;
      if (next_process) {
        // skip to the time of this item's arrival
          current_time = next_process->arrival_time;
          temp_process = TAILQ_NEXT(next_process, pointers); 
          // add it to the ready queue 
          TAILQ_REMOVE(&list, next_process, pointers);
          TAILQ_INSERT_TAIL(&ready_queue, next_process, pointers);
          next_process = temp_process;  
      }
    }

    struct process *current_process = TAILQ_FIRST(&ready_queue);

    // once a process starts, we can calculate the response time
    if (current_process->start_exec_time == (u32)-1) {
        current_process->start_exec_time = current_time;
        int response_time = current_time - current_process->arrival_time;
        total_response_time += response_time;
    }

    // the process will run either as long as the quantum time or until completion
    u32 run_time = (current_process->remaining_time < quantum_length) ? current_process->remaining_time : quantum_length;
    u32 end_time = current_time + run_time;

    // here, we queue processes that arrive while this process is running
    struct process *next_proc, *tmp_proc;
    TAILQ_FOREACH_SAFE(next_proc, &list, pointers, tmp_proc) {
        // if the process arrives between the current time and the end time AND it hasnt already run, we'll queue it
        if (next_proc->arrival_time > current_time && next_proc->arrival_time <= end_time && next_proc->start_exec_time == (u32)-1) {
            TAILQ_REMOVE(&list, next_proc, pointers);
            TAILQ_INSERT_TAIL(&ready_queue, next_proc, pointers);
        }
    }

    // move the clock forward
    current_time += run_time;
    current_process->remaining_time -= run_time;
    
    // remove the process, and decide whether to re-queue it 
    TAILQ_REMOVE(&ready_queue, current_process, pointers);
    if (current_process->remaining_time > 0) {
      // it should go to the end of the queue if it still has time to run
      TAILQ_INSERT_TAIL(&ready_queue, current_process, pointers);
      } else {
        // if the process has completed, we can calculate its waiting time
        total_waiting_time += current_time - current_process->arrival_time - current_process->burst_time;
      }
    }

  /* End of "Your code here" */

  printf("Average waiting time: %.2f\n", (float)total_waiting_time / (float)size);
  printf("Average response time: %.2f\n", (float)total_response_time / (float)size);

  free(data);
  return 0;
}
