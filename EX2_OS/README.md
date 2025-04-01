# uthreads - User-Level Threads Library

This project implements a user-level threads library in C++, based on the API provided by the Hebrew University operating systems course.

## Features

- Thread creation and termination
- Thread blocking, resuming, and sleeping
- Preemptive scheduling using a virtual timer (`SIGVTALRM`)
- Independent thread stacks
- Time-slice (quantum) management and tracking
- Support for up to 100 concurrent threads

## API

The library exposes the following functions (defined in `uthreads.h`):

```cpp
int uthread_init(int quantum_usecs);
int uthread_spawn(thread_entry_point entry_point);
int uthread_terminate(int tid);
int uthread_block(int tid);
int uthread_resume(int tid);
int uthread_sleep(int num_quantums);
int uthread_get_tid();
int uthread_get_total_quantums();
int uthread_get_quantums(int tid);
```

See `uthreads.h` for detailed documentation of each function.

## Design Overview

- Threads are managed using an internal `Thread` class that stores their stack, block status, sleep status, and quantum count.
- A `ThreadTidManager` handles allocation and reuse of thread IDs.
- Context switching is implemented using `sigsetjmp`/`siglongjmp` and low-level address manipulation for setting stack and program counters.
- The main thread (TID 0) is treated specially and cannot be blocked or put to sleep.
- Preemption is achieved using `setitimer` with `SIGVTALRM` to implement time slices (quantums).
- Thread data is stored in a global structure (`ThreadGlobals`) for centralized management.

## Build and Usage

To compile:
```bash
g++ -std=c++11 -Wall -Wextra uthreads.cpp -o uthreads
```

To use the library in your own program, include `uthreads.h` and link with the compiled `uthreads.o` file.

Example usage:
```cpp
#include "uthreads.h"

void f() {
  // do some work
}

int main() {
  uthread_init(100000); // 100ms quantum
  uthread_spawn(f);
  while (true) {}
  return 0;
}
```

## Limitations

- No support for thread priorities
- Not designed for multi-core or true parallel execution (single-core simulation only)
- Not thread-safe (assumes single-threaded environment)

## License

Provided for educational use as part of the Hebrew University OS course.
