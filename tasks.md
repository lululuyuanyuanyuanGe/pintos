Based on the project documentation, here is a summary of the tasks required for Project 1: Threads:


  1. Alarm Clock
  Goal: Reimplement timer_sleep() in devices/timer.c to eliminate "busy waiting" (looping while checking the time).
   * Requirement: The calling thread must suspend execution and yield the CPU until at least x timer ticks have passed.
   * Implementation: Instead of spinning, the thread should block and be put to sleep, then woken up by the timer
     interrupt when enough time has elapsed.


  2. Priority Scheduling
  Goal: Modify the scheduler so that higher-priority threads always run before lower-priority ones.
   * Priority Range: 0 (lowest) to 63 (highest).
   * Preemption: If a thread is added to the ready list with a higher priority than the currently running thread, the
     current thread must yield the processor immediately.
   * Waiting: When threads are waiting for a synchronization primitive (lock, semaphore, condition variable), the
     highest priority waiting thread must be woken up first.


  3. Priority Donation
  Goal: Implement priority donation to prevent "priority inversion."
   * Scenario: If a high-priority thread (H) waits for a lock held by a low-priority thread (L), H cannot run. If a
     medium-priority thread (M) runs, it prevents L from finishing and releasing the lock, effectively blocking H
     indefinitely.
   * Solution: H should "donate" its priority to L while L holds the lock.
   * Requirements:
       * Handle multiple donations (multiple high-priority threads waiting on one low-priority thread).
       * Handle nested donations (H waits on M, M waits on L -> L gets H's priority).
       * This is specifically required for locks.


  4. Priority API
  Goal: Implement the user-facing functions in threads/thread.c.
   * void thread_set_priority(int new_priority): Set the current thread's priority. Yield if it no longer has the
     highest priority.
   * int thread_get_priority(void): Return the current thread's effective priority (taking donations into account).