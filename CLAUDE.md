# Pintos Project 2: User Programs

## Overview
This is a Pintos OS project (Project 2 - User Programs). The goal is to enable user programs to interact with the OS via system calls, building on top of the base kernel.

## Build & Run Environment
- Pintos runs inside a **Docker container**: `thierrysans/pintos`
- Platform flag: `--platform linux/amd64`
- Start interactive container with source mount:
  ```
  docker run --platform linux/amd64 --rm --name pintos -it -v "$(pwd):/pintos" thierrysans/pintos bash
  ```
- Build from inside the container:
  ```
  cd /pintos/src/userprog && make
  ```
- Run all tests:
  ```
  cd /pintos/src/userprog/build && make check
  ```
- Run a single test (e.g. `args-single`):
  ```
  make tests/userprog/args-single.result
  ```
- Simulators: **Bochs** (default, reproducible) and **QEMU** (faster, use `--qemu`)
- Use `SIMULATOR=--qemu` for faster test runs: `make check SIMULATOR=--qemu`
- Verbose output: `make check VERBOSE=1`

## Project Structure (Key Files)
```
src/
├── userprog/
│   ├── process.c      — Process loading, argument passing, wait/exit (MAIN WORK)
│   ├── process.h
│   ├── syscall.c      — System call handler and implementations (MAIN WORK)
│   ├── syscall.h
│   ├── exception.c    — Page fault handler (may need modification)
│   ├── pagedir.c/h    — Page table management (read-only, call its functions)
│   ├── gdt.c/h        — Global Descriptor Table (do not modify)
│   └── tss.c/h        — Task-State Segment (do not modify)
├── threads/
│   ├── thread.c/h     — Thread struct with per-process data (modified)
│   ├── synch.c/h      — Synchronization primitives (semaphores, locks)
│   ├── vaddr.h        — Virtual address utilities (PHYS_BASE, is_user_vaddr)
│   └── init.c         — Kernel main, calls process_wait()
├── filesys/
│   ├── filesys.c/h    — File system operations (DO NOT MODIFY)
│   └── file.c/h       — File operations (DO NOT MODIFY)
├── lib/
│   ├── user/syscall.c — User-space syscall wrappers
│   ├── user/entry.c   — _start() entry point for user programs
│   ├── syscall-nr.h   — System call number definitions
│   └── string.h/c     — strtok_r() and other string functions
├── examples/          — Sample user programs
└── tests/userprog/    — Test cases (do not modify for submission)
```

## Current Implementation Status

### Completed
- **Argument passing** in `setup_stack()` — parses command line, pushes args onto stack per 80x86 convention
- **System call infrastructure** — `syscall_handler()` reads syscall number, dispatches to handlers
- **User memory validation** — `is_valid_ptr()`, `is_valid_buffer()`, `is_valid_string()` check pointers before dereference
- **File descriptor table** — per-process `fd_table[MAX_FD]` array in `struct thread`
- **File system lock** — global `filesys_lock` protects concurrent file system access
- **Process termination messages** — `exit()` prints `"%s: exit(%d)\n"`
- **Parent-child relationship** — `child_info` struct with semaphore for wait/exit synchronization
- **Implemented syscalls**: halt, exit, write, create, remove, open, close, read, filesize, seek, tell
- **process_wait()** — finds child in list, blocks on semaphore, returns exit status
- **process_exit()** — closes open files, signals parent, frees children list, releases locks

### Still TODO / Needs Verification
- **exec syscall** — `SYS_EXEC` case has `// TODO: implement exec` (needs `process_execute` + load synchronization)
- **wait syscall** — `SYS_WAIT` case has `// TODO: implement wait` (needs to call `process_wait`)
- **Denying writes to executables** — `file_deny_write()` not yet called on loaded executables; the file is closed in `load()` done label
- **Load synchronization for exec** — parent must know if child loaded successfully before returning from exec

## Architecture & Data Structures

### struct thread (in thread.h) — USERPROG additions:
- `uint32_t *pagedir` — page directory
- `struct list children` — list of `child_info` structs
- `struct child_info *child_info_in_parent` — pointer to this thread's entry in parent's list
- `int exit_status` — set by `exit()` syscall
- `struct file *fd_table[MAX_FD]` — file descriptor table (fd 0,1 reserved)
- `struct list acquired_locks` — locks held by this process

### struct child_info (in process.c):
- `tid_t tid`, `int exit_status`, `bool waited`
- `struct semaphore sema` — parent blocks here until child exits
- `struct list_elem elem`

### struct process_start_info (in process.c):
- `char *file_name` — full command line copy
- `char *prog_name` — just the program name
- `struct child_info *ci`

## System Calls (lib/syscall-nr.h numbers)
| # | Name | Args | Return |
|---|------|------|--------|
| 0 | SYS_HALT | void | void |
| 1 | SYS_EXIT | int status | void |
| 2 | SYS_EXEC | const char *cmd_line | pid_t |
| 3 | SYS_WAIT | pid_t pid | int |
| 4 | SYS_CREATE | const char *file, unsigned size | bool |
| 5 | SYS_REMOVE | const char *file | bool |
| 6 | SYS_OPEN | const char *file | int fd |
| 7 | SYS_FILESIZE | int fd | int |
| 8 | SYS_READ | int fd, void *buf, unsigned size | int |
| 9 | SYS_WRITE | int fd, const void *buf, unsigned size | int |
| 10 | SYS_SEEK | int fd, unsigned pos | void |
| 11 | SYS_TELL | int fd | unsigned |
| 12 | SYS_CLOSE | int fd | void |

## Key Rules
- **Bulletproof kernel**: No user program should crash the OS. Validate all user pointers.
- **File system is not thread-safe**: Always hold `filesys_lock` when calling filesys/file functions.
- **No modifying filesys/ directory** code.
- **process_execute() also accesses files** — needs synchronization.
- **Each process has independent file descriptors** — not inherited by children.
- **Free all resources** whether parent waits or not.
- **Stack setup** follows 80x86 calling convention: args pushed right-to-left, word-aligned, with fake return address.

## Coding Style
- Follow existing Pintos C style (2-space indentation, GNU-style braces)
- Add brief comments on every new struct, member, global, and function
- Use Pintos list library (`<list.h>`) for linked lists
- Use `strtok_r()` for string tokenization (reentrant)
- Use `palloc_get_page()` / `malloc()` for dynamic allocation
