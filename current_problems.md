# Current Problems & Required Fixes - Project 1 (Threads)

Based on a thorough analysis of `src/threads/` and `src/devices/timer.c`, here are the current deficiencies and the necessary steps to resolve them.

## 1. Alarm Clock
**Current Status:**
*   `timer_sleep` (in `devices/timer.c`) **does not** use busy waiting. It correctly creates a `timer_sleeper` struct, adds it to a `sleeping_list`, and blocks using a semaphore.
*   `timer_interrupt` iterates through the `sleeping_list` to wake up threads.

**Problem:**
1.  **Inefficiency:** `timer_interrupt` iterates through the **entire** `sleeping_list` on every single tick. This is O(N) per tick.
2.  **Stack Corruption Risk:** The `struct timer_sleeper` is allocated on the **stack** of the sleeping thread.
    ```c
    void timer_sleep (int64_t ticks) {
      struct timer_sleeper sleeper; // Local variable on stack
      ...
      list_push_back (&sleeping_list, &sleeper.elem);
      ...
      sema_down (&sleeper.sema);
    }
    ```
    *   While the thread is blocked on `sema_down`, its stack frame is valid.
    *   Once `sema_up` is called by the interrupt handler, the thread wakes up and returns from `timer_sleep`, destroying the stack frame.
    *   **However**, `timer_interrupt` accesses `sleeper` (and its sema) *before* the thread wakes up, so this specific usage is technically safe from a "use-after-free" perspective during the wake-up sequence itself.
    *   **BUT**, there is a race condition. If `timer_interrupt` preempts `timer_sleep` *after* `list_push_back` but *before* `sema_down`, the sleeper will be waked up immediately (if time has passed), and then `timer_sleep` will call `sema_down` on a semaphore that has already been upped (value 1), so it won't sleep. This is actually handled correctly by semaphores (value becomes 0, thread continues).
    *   **Crucial Issue:** The `sleeping_list` is unordered.

**Fix:**
*   **Ordered List:** Change `list_push_back` to `list_insert_ordered` in `timer_sleep`.
*   **Optimized Interrupt:** Update `timer_interrupt` to check only the front of the list. Break the loop as soon as a sleeper with `wakeup_tick > ticks` is found.

## 2. Priority Scheduling
**Current Status:** Partially implemented.
*   `ready_list` is kept sorted by priority (`list_insert_ordered` in `thread_unblock` and `thread_yield`).
*   `next_thread_to_run` correctly picks the head of the `ready_list`.
*   Preemption is checked in `thread_create` and `thread_set_priority`.

**Problem:**
*   The preemption logic relies on `t->priority`, which is currently just a single value. It doesn't account for priority donation.
*   Wait queues for synchronization primitives (locks, semaphores, condition variables) are **FIFOs** (First-In-First-Out), not priority queues.

**Fix:**
*   **Synchronization Waiters:** Modify `sema_down` and `cond_wait` (in `synch.c`) to use `list_insert_ordered` for their wait lists (`sema->waiters`, `cond->waiters`), ensuring high-priority threads wake up first.
*   **Preemption:** Ensure `thread_yield()` is called immediately whenever a higher-priority thread becomes ready (already mostly there, but needs to be robust with donation).

## 3. Priority Donation (Priority Inversion)
**Current Status:** **Completely Missing.**
*   `struct thread` has only one `priority` field.
*   `struct lock` has no knowledge of which thread holds it (other than a debug pointer) or a list of waiters to donate priority.
*   `thread_set_priority` blindly overwrites the current priority.

**Problem:** A low-priority thread holding a lock will not run if a medium-priority thread is runnable, starving the high-priority thread waiting for that lock.

**Fix:**
*   **Data Structures (thread.h):**
    *   Add `int base_priority;` to `struct thread` (to store the original priority).
    *   Add `struct list locks_held;` to `struct thread` (to track locks this thread owns).
    *   Add `struct lock *lock_waiting;` to `struct thread` (to track the lock blocking this thread, for nested donation).
    *   Add `struct list_elem elem;` (or similar) to `struct lock` so it can be put in `locks_held`.
*   **Logic Changes (synch.c & thread.c):**
    *   **`lock_acquire`:**
        *   Before blocking, check if the lock holder has a lower priority.
        *   If so, **donate** the current thread's priority to the holder.
        *   Recurse (nested donation): If the holder is also waiting on a lock, donate to *that* lock's holder too.
    *   **`lock_release`:**
        *   Remove the released lock from `locks_held`.
        *   **Recalculate Priority:** Reset the current thread's priority to its `base_priority`, then check all remaining locks in `locks_held`. Maximize priority with the highest priority waiter from each held lock.
    *   **`thread_set_priority`:**
        *   Only update `base_priority`.
        *   Recalculate effective `priority` (it might stay high if donations exist).
        *   Yield if the effective priority drops below the highest ready thread.

## 4. Priority API
**Current Status:** Naive implementation.
**Problem:** `thread_set_priority` breaks donation logic by overwriting the value. `thread_get_priority` returns the raw value, which is fine *if* we maintain `priority` as the effective priority.

**Fix:**
*   **`thread_set_priority`:** Update `base_priority` only. Call a helper function `thread_update_priority()` to re-evaluate the effective priority based on `base_priority` and donations.
*   **`thread_get_priority`:** Should return the effective `priority` (which handles the donation requirement automatically if our logic is correct).

---
## Summary of Action Plan
1.  **Modify `struct thread`**: Add `base_priority`, `locks_held`, `lock_waiting`.
2.  **Implement Priority Donation**:
    *   Update `lock_acquire` to donate.
    *   Update `lock_release` to recall donations.
    *   Implement `thread_update_priority` helper.
3.  **Fix `thread_set_priority`**: Use `base_priority`.
4.  **Fix Synchronization Queues**: Make semaphore and condition variable waiters priority-ordered.
5.  **Optimize Alarm Clock**: Change `sleeping_list` to be ordered and optimize `timer_interrupt` to O(1) check.