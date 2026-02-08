# Pintos Documentation

## 1. Getting Started

### 1.1 Docker & Building

**Run Container:**
```bash
docker run --platform linux/amd64 --rm --name pintos -w /pintos/src/threads/build thierrysans/pintos pintos -q run alarm-zero
```

**Build Utils (First time only):**
```bash
cd /pintos/src/utils
make
```

**Build Threads Kernel:**
```bash
cd /pintos/src/threads
make
```

**Run Pintos (Bochs default):**
```bash
cd /pintos/src/threads/build
pintos -q run alarm-multiple
```

**Run with QEMU:**
```bash
pintos --qemu -- -q run alarm-multiple
```

### 1.2 Testing

**Run all tests:**
```bash
make check
```

**Run individual test:**
```bash
make tests/threads/alarm-multiple.result
# If 'up-to-date', delete .output file or run make clean
```

**Debug Flags:**
- `VERBOSE=1`: Show progress during run.
- `PINTOSOPTS='-j 1'`: Enable jitter (Bochs only) for timing variability.

### 1.3 Source Tree
- `threads/`: Base kernel (Project 1).
- `userprog/`: User program loader (Project 2).
- `vm/`: Virtual memory (Project 3).
- `filesys/`: File system (Project 4).
- `devices/`: I/O (Timer, Keyboard, etc.).
- `lib/`: Standard C library subset.
- `tests/`: Project tests.

---

## C. Coding Standards

### C.1 C99 Features
- `<stdbool.h>`: `bool`, `true`, `false`.
- `<stdint.h>`: `int8_t`, `uint32_t`, `intptr_t`, `uintptr_t`, `intmax_t`, `uintmax_t`.
- `<inttypes.h>`: Printf macros (e.g., `PRId32`, `PRIu64`).
- `<stdio.h>` modifiers: `%jd` (intmax), `%zu` (size_t), `%td` (ptrdiff_t).

### C.2 Unsafe String Functions (Banned)
Use the replacements found in `lib/string.c` and `lib/stdio.h`:
- `strcpy` -> `strlcpy`
- `strncpy` -> `strlcpy`
- `strcat` -> `strlcat`
- `strncat` -> `strlcat`
- `strtok` -> `strtok_r`
- `sprintf` -> `snprintf`
- `vsprintf` -> `vsnprintf`

---

## 2. Project 1: Threads

**Work Directory:** `threads/` (and `devices/timer.c`).

### 2.1 Requirements

#### 2.1.1 Alarm Clock
**Goal:** Reimplement `timer_sleep()` in `devices/timer.c` to avoid busy waiting.
**Function:** `void timer_sleep (int64_t ticks)`
- Suspend thread execution for at least `x` ticks.
- Use `TIMER_FREQ` (default 100 Hz).
- Do not change `timer_msleep`, `timer_usleep`, or `timer_nsleep`.

#### 2.1.2 Priority Scheduling
**Goal:** Implement strict priority scheduling.
- **Priority Range:** 0 (`PRI_MIN`) to 63 (`PRI_MAX`). Default 31 (`PRI_DEFAULT`).
- **Preemption:** Yield immediately if a higher priority thread is added to the ready list.
- **Waiting:** Highest priority thread waiting on lock/sema/cond should wake first.
- **Priority Donation:**
  - Fix priority inversion.
  - Handle multiple donations and nested donations (depth limit ~8).
  - Required for locks.
- **API:**
  - `void thread_set_priority (int new_priority)`: Set priority, yield if no longer highest.
  - `int thread_get_priority (void)`: Return effective (donated) priority.

### 2.2 FAQ & Reference

**Key Macros:**
- `TIMER_FREQ`: Ticks per second (default 100).
- `TIME_SLICE`: Ticks per time slice (default 4).

**Common Issues:**
- **Starvation:** Expected with strict priority scheduling.
- **Donation:** Not additive. Donated priority overrides base priority if higher.
- **Donation Reset:** When lock is released, donee's priority should revert to base (or next highest donation).
- **Output Interleaving:** If test names appear doubled (e.g., `(alarm-priority) (alarm-priority)`), a lower priority thread is preempting a higher one during printing, indicating a scheduler bug.

**Interrupt Handling:**
- Code in `devices/` (timer) runs in interrupt context.
- Use `intr_disable()` / `intr_set_level()` for synchronization in kernel threads when sharing data with interrupt handlers.
- Do not sleep in interrupt handlers.

**Important Files:**
- `threads/thread.c`: Thread management struct and functions.
- `devices/timer.c`: Timer interrupt and sleep.
- `threads/synch.c`: Semaphores, locks, condition variables.
- `threads/switch.S`: Context switching assembly.