#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, const char *prog_name,
                  void (**eip) (void), void **esp);

// Data structure that represents a child process in a parent's list
struct child_info
  {
    tid_t tid;                /* child's thread id */
    int exit_status;          /* exit code set by the child */
    bool waited;              /* prevent parent calling multiple waits */
    struct semaphore sema;    /* to block parent until child exits */
    struct list_elem elem;    /* element in parent's 'children' list */
  };

/* Data structure that store info that the child will need on start.
   Passed in as AUX in the thread_create function */
struct process_start_info
  {
    char *file_name;       // Full command line copy 
    char *prog_name;       // Just the program name 
    struct child_info *ci;
  };

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
  Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  // Extract program name
  size_t prog_len = 0;
  while (file_name[prog_len] != '\0' && file_name[prog_len] != ' ')
    prog_len++;
  char *prog_name = malloc (prog_len + 1);
  if (prog_name == NULL)
    {
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  memcpy (prog_name, file_name, prog_len);
  prog_name[prog_len] = '\0';

  // Initialize child_info
  struct child_info *ci;
  ci = malloc (sizeof *ci);
  if (ci == NULL)
    {
      free (prog_name);
      palloc_free_page (fn_copy);
      return TID_ERROR;
    }
  ci->tid = -1; // filled in once thread_create returns tid
  ci->exit_status = -1; // will be set during exit() system call
  ci->waited = false;
  sema_init (&ci->sema, 0);

  // Initialize process_start_info
  struct process_start_info *psi;
  psi = malloc (sizeof *psi);
  if (psi == NULL)
    {
      palloc_free_page (fn_copy);
      free (ci);
      free (prog_name);
      return TID_ERROR;
    }
  psi->file_name = fn_copy;
  psi->prog_name = prog_name;
  psi->ci = ci;

  // Create a new thread to execute FILE_NAME stored in psi
  tid = thread_create (prog_name, PRI_DEFAULT, start_process, psi);
  if (tid == TID_ERROR)
    {
      palloc_free_page (fn_copy);
      free (ci);
      free (prog_name);
      free (psi);
      return TID_ERROR;
    }

  // Record child's tid in status struct and add to our list
  ci->tid = tid;
  list_push_back (&thread_current ()->children, &ci->elem);
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *process_info)
{
  struct process_start_info *psi = process_info;
  char *file_name = psi->file_name;
  char *prog_name = psi->prog_name;
  struct child_info *ci = psi->ci;
  struct intr_frame if_;
  bool success;

  /* Store pointer to our child_info so that process_exit can find it
     and use it to store the exit status */
  thread_current ()->child_info_in_parent = ci;

  /* Initialize file descriptor table: reserve 0 and 1 for stdin/stdout */
  struct thread *cur = thread_current ();
  for (int i = 2; i < MAX_FD; i++)
    {
      cur->fd_table[i] = NULL;
    }

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, prog_name, &if_.eip, &if_.esp);

  /* If load failed, quit. */
  palloc_free_page (file_name);
  free (prog_name);
  free (psi);
  if (!success) 
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid) 
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct child_info *ci = NULL;

  // Check if child_tid is in our list of children
  for (e = list_begin (&cur->children); e != list_end (&cur->children);
       e = list_next (e))
    {
      struct child_info *c = list_entry (e, struct child_info, elem);
      if (c->tid == child_tid)
        {
          ci = c;
          break;
        }
    }

  // If child is null, that means we could not find it in our list
  if (ci == NULL)
    return -1;

  // Only one wait per child is permitted
  if (ci->waited)
    return -1;
  ci->waited = true;

  // Blocks until child has exited (it will call sema_up)
  sema_down (&ci->sema);

  // Store the exit status
  int status = ci->exit_status;

  // Remove the child from list and free it
  list_remove (&ci->elem);
  free (ci);

  return status;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  /*
    Notes:
      - psi is freed in start_process
      - psi->file_name is freed in start_process
      - psi->prog_name is freed in start_process
      - when parent terminates, free all ci in children list
        using list_entry to get struct child_info in a loop
  */

  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Close all open files */
  for (int i = 2; i < MAX_FD; i++)
    {
      if (cur->fd_table[i] != NULL)
        {
          file_close (cur->fd_table[i]);
          cur->fd_table[i] = NULL;
        }
    }

  /* If we were created by a parent and it still exists, signal our
     parent that we're exiting
  */
  if (cur->child_info_in_parent != NULL)
    {
      cur->child_info_in_parent->exit_status = cur->exit_status;
      sema_up (&cur->child_info_in_parent->sema);
    }

  // Free children list
  struct list_elem *e = list_begin(&cur->children);
  while (e != list_end(&cur->children))
    {
      struct list_elem *next = list_next(e);
      struct child_info *child = list_entry(e, struct child_info, elem);
      list_remove(e);
      free(child);
      e = next;
    }

  // Release all the locks that this process was holding
  while (!list_empty(&cur->acquired_locks))
    {
      struct lock *lock = list_entry(list_pop_front(&cur->acquired_locks),
                                    struct lock, elem);
      lock_release(lock);
    }

  // TODO: what about cond vars and semas?

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char *file_name);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, const char *prog_name,
      void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (prog_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, (char *) file_name))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char *file_name) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success){
        *esp = PHYS_BASE;

        bool space_before = false;
        int argc = 1; // Starts at one to include filename
        for (int i = 0; file_name[i] != '\0'; i++) {
          if (file_name[i] == ' ')
            {
              if (!space_before) {
                argc++; // Each space represents an argument
                space_before = true;
              }
            }
          else {
            space_before = false;
          }
        }
        const int SENTINEL = 4;
        const int BYTES_FOR_RETURN_ARGC_ARGV = 12;

        int file_name_size = strlen(file_name) + 1; // +1 represents extra delimiter
        int word_align_size = (4 - (file_name_size % 4)) % 4;
        int args_address_size = 4 * argc;

        // This offset represents the size in bytes of all the 
        // arguments + the word align size + sentinel
        int offset = file_name_size + word_align_size + SENTINEL;
        int total_byte_size = offset + args_address_size + BYTES_FOR_RETURN_ARGC_ARGV;

        // Check if we overflow the page
        if (total_byte_size > PGSIZE){
          palloc_free_page (kpage);
          return false;
        }
        
        // This represents the byte that we start at to store argv[0]
        int argv_string_start = -file_name_size;
        // This represents the byte that we start at to store address of argv[0]
        int argv_address_start = -offset - args_address_size;
        
        memset(*esp - total_byte_size + 4, argc, 1); // Set argc

        // Set argv
        uint32_t argv = (uint32_t) (*esp + argv_address_start);
        
        // Save the address to the corresponding bytes using
        // bit shifting to get the 2 relevant hex digits to the end,
        // and bit masking to only get the last 2 hex digits with 0xFF.
        // Since each hex digit counts up to 16 (has 4 bits), to shift
        // 2 hex digits every, we need to shift 8 bits
        for(int i = 0; i < 4; i++){
          memset(*esp - total_byte_size + 8 + i, (argv) & 0xFF, 1);
          argv >>= 8;
        }
        
        // Push args and its address onto stack
        char *token, *save_ptr;

        for (token = strtok_r (file_name, " ", &save_ptr); token != NULL;
        token = strtok_r (NULL, " ", &save_ptr)){
          // Store memory address. Follows same bit shifting and bit masking
          // strategy as storing argv above
          uint32_t addr = (uint32_t) (*esp + argv_string_start);

          for(int i = 0; i < 4; i++){
            memset(*esp + argv_address_start++, (addr) & 0xFF, 1);
            addr >>= 8;
          }

          // Storing each character of the arguments onto the stack
          int token_length = strlen(token);
          for(int i = 0; i < token_length; i++){
            memset(*esp + argv_string_start, token[i], 1);
            argv_string_start++;
          }

          argv_string_start++; // This is to give one byte to null pointer
        }

        // Set esp to top of stack
        *esp = PHYS_BASE - total_byte_size;

        //hex_dump((uintptr_t)PHYS_BASE - total_byte_size, *esp, total_byte_size, true);
      }
      else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
