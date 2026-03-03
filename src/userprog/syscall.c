#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/shutdown.h"
#include "threads/synch.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);
// Used to synchronize filesys calls
static struct lock filesys_lock;

// System call implementations
void exit (int status);
void halt (void);
int write (int fd, const void *buffer, unsigned size);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
void close (int fd);
int read (int fd, void *buffer, unsigned size);
int filesize (int fd);
unsigned tell (int fd);
void seek (int fd, unsigned pos);

// Helper functions
struct file *get_file_by_fd (int fd);
bool is_valid_ptr (const void *vaddr);
bool is_valid_buffer (const void *buffer, size_t size);
bool is_valid_string (const char *str);
int allocate_fd(struct file *file);

/* Exits the current thread with the given status. */
void
exit(int status)
{
  struct thread *t = thread_current ();
  printf("%s: exit(%d)\n", t->name, status);
  t->exit_status = status;
  thread_exit();
}

// Terminates Pintos by calling shutdown_power_off()
void
halt (void)
{
  shutdown_power_off();
}

/* Writes size bytes from buffer to the open file fd.
   Returns the number of bytes actually written, which may be
   less than size if some bytes could not be written. */
int
write (int fd, const void *buffer, unsigned size)
{
  if (size == 0)
    return 0;

  // Handle invalid fds, including stdin (thus fd < 1)
  if (fd < STDOUT_FILENO || fd >= MAX_FD)
    return -1;

  // Handle stdout
  if (fd == STDOUT_FILENO)
    {
      if (size <= 256) // Limit size to 256 bytes per write
        {
          putbuf((const char *)buffer, size);
        }
      else
        {
          unsigned MAX_CHUNK_SIZE = 256;
          unsigned written = 0;
          while (written < size)
            {
              unsigned chunk_size = (size - written > MAX_CHUNK_SIZE) ? MAX_CHUNK_SIZE : (size - written);
              putbuf((const char *)buffer + written, chunk_size);
              written += chunk_size;
            }
        }
      return size;
    }

  // Handles other files (fd >= 2)
  struct file *file = get_file_by_fd(fd);
  if (file == NULL)
    return 0;

  lock_acquire(&filesys_lock);
  int written = file_write(file, buffer, size);
  lock_release(&filesys_lock);
  return written;
}

/* Creates a new file named file with the given initial size in bytes.
   Returns true if successful, false otherwise. */
bool
create (const char *file, unsigned initial_size)
{
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

/* Removes the file named file.
   Returns true if successful, false otherwise. */
bool
remove (const char *file)
{
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

int
open (const char *file)
{
  lock_acquire(&filesys_lock);
  struct file *f = filesys_open(file);
  lock_release(&filesys_lock);
  if (f == NULL)
    return -1;

  int fd = allocate_fd(f);
  if (fd == -1){
    lock_acquire(&filesys_lock);
    file_close(f); // No available fd, close the file
    lock_release(&filesys_lock);
  }

  return fd;
}

void
close (int fd)
{
  if (fd < 2 || fd >= MAX_FD)
    return;

  struct thread *cur = thread_current();
  struct file *f = cur->fd_table[fd];
  if (f != NULL)
    {
      lock_acquire(&filesys_lock);
      file_close(f);
      lock_release(&filesys_lock);
      cur->fd_table[fd] = NULL;
    }
}

int
read (int fd, void *buffer, unsigned size)
{
  if (size == 0)
    return 0;

  // Handle invalid fds, including stdout
  if (fd < 0 || fd == STDOUT_FILENO || fd >= MAX_FD)
    return -1;

  // Handle stdin
  if (fd == STDIN_FILENO)
    {
      int bytes_read = 0;
      for (unsigned i = 0; i < size; i++) {
        int key = input_getc();
        ((uint8_t *)buffer)[i] = (uint8_t)key;
        bytes_read++;
      }
      return bytes_read;
    }

  // Handles other files (fd >= 2)
  struct file *file = get_file_by_fd(fd);
  if (file == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  int bytes_read = file_read(file, buffer, size);
  lock_release(&filesys_lock);
  if (bytes_read < 0)
    return -1;
  return bytes_read;
}

/* Returns the size of the file in bytes with the given fd.
   Returns -1 if the fd is invalid. */
int
filesize (int fd)
{
  struct file *f = get_file_by_fd(fd);
  if (f == NULL) return -1;

  return file_length(f);
}

/* Seeks to the given position in the file with the given fd. */
void
seek (int fd, unsigned position)
{
  struct file *f = get_file_by_fd(fd);
  if (f == NULL) return;

  file_seek(f, position);
}

/* Returns the position of the next byte to be read from
   from the file with the given fd.
   Returns -1 if the fd is invalid. */
unsigned
tell (int fd)
{
  struct file *f = get_file_by_fd(fd);
  if (f == NULL) return -1;

  return file_tell(f);
}

/* Handler for system calls */
static void syscall_handler (struct intr_frame *f)
{
  // validate the endpoints since there's no need to do
  // page validation on 4 bytes.
  uint8_t *esp_end = (uint8_t *)f->esp + sizeof(int) - 1;
  if (!is_valid_ptr (f->esp) || !is_valid_ptr (esp_end))
  {
    exit(-1);
    return;
  }

  int *args = (int *)f->esp;

  int syscall_num = args[0];

  // check which system call we have
  switch (syscall_num)
  {
    case SYS_HALT:
    {
      halt();
      break;
    }
    case SYS_EXIT:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      exit(args[1]);
      break;
    }
    case SYS_EXEC:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      if (!is_valid_string ((const char *)args[1]))
      {
        exit(-1);
        return;
      }
      // TODO: implement exec
      break;
    }
    case SYS_WAIT:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      // TODO: implement wait
      break;
    }
    case SYS_CREATE:
    {
      if (!is_valid_buffer (&args[1], sizeof(int) * 2))
      {
        exit(-1);
        return;
      }
      if (!is_valid_string ((const char *)args[1]))
      {
        exit(-1);
        return;
      }
      const char *file = (const char *)args[1];
      unsigned size = (unsigned)args[2];
      f->eax = create(file, size);
      break;
    }
    case SYS_REMOVE:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      if (!is_valid_string ((const char *)args[1]))
      {
        exit(-1);
        return;
      }
      const char *file = (const char *)args[1];
      f->eax = remove(file);
      break;
    }
    case SYS_OPEN:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      if (!is_valid_string ((const char *)args[1]))
      {
        exit(-1);
        return;
      }
      const char *file = (const char *)args[1];

      int fd = open(file);
      f->eax = fd;
      break;
    }
    case SYS_FILESIZE:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      f->eax = filesize(fd);
      break;
    }
    case SYS_READ:
    {
      if (!is_valid_buffer (&args[1], sizeof(int) * 3))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      void *buffer = (void *)args[2];
      unsigned size = (unsigned)args[3];
      if (!is_valid_buffer (buffer, size))
      {
        exit(-1);
        return;
      }
      int bytes_read = read(fd, buffer, size);
      f->eax = bytes_read;
      break;
    }
    case SYS_WRITE:
    {
      if (!is_valid_buffer (&args[1], sizeof(int) * 3))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      const void *buffer = (const void *)args[2];
      unsigned size = (unsigned)args[3];
      if (!is_valid_buffer (buffer, size))
      {
        exit(-1);
        return;
      }
      int written = write(fd, buffer, size);
      f->eax = written;
      break;
    }
    case SYS_SEEK:
    {
      if (!is_valid_buffer (&args[1], sizeof(int) * 2))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      unsigned pos = (unsigned)args[2];
      seek(fd, pos);
      break;
    }
    case SYS_TELL:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      f->eax = tell(fd);
      break;
    }
    case SYS_CLOSE:
    {
      if (!is_valid_buffer (&args[1], sizeof(int)))
      {
        exit(-1);
        return;
      }
      int fd = (int)args[1];
      close(fd);
      break;
    }
    default:
    {
      // Unknown system call, process is killed
      exit(-1);
      break;
    }
  }
}

void
syscall_init (void)
{
  // Initialize filesys_lock
  lock_init(&filesys_lock);

  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/******************** Helper Functions ********************/

/* Returns true if VADDR is a valid user virtual address. */
bool is_valid_ptr (const void *vaddr)
{
  if (vaddr == NULL)
    return false;

  if (!is_user_vaddr (vaddr))
    return false;

  if (pagedir_get_page (thread_current ()->pagedir, vaddr) == NULL)
    return false;

  return true;
}

/* Returns true if BUFFER is a valid user buffer of SIZE bytes. */
bool is_valid_buffer (const void *buffer, size_t size)
{
  if (size == 0) return true;
  if (buffer == NULL) return false;

  const uint8_t *start = buffer;
  const uint8_t *end = start + size;

  // Begin at the page boundary to check by page in the buffer.
  const uint8_t *page_ptr = pg_round_down(start);

  for (; page_ptr < end; page_ptr += PGSIZE)
  {
    // Due to pages being contiguous, we only need to check
    // the first byte of each page.

    // The first page_ptr may not be in buffer, check the
    // start of buffer in that case
    const uint8_t *check_ptr = (page_ptr < start) ? start : page_ptr;

    if (!is_valid_ptr(check_ptr)) return false;
  }

  return true;
}

/* Returns true if STR is a valid user string. */
bool is_valid_string (const char *str)
{
  if (str == NULL) return false;

  while (1)
  {
    if (!is_valid_ptr(str)) return false;
    if (*str == '\0') break;
    str++;
  }

  return true;
}

/* Get file by file descriptor */
struct file *
get_file_by_fd (int fd)
{
  if (fd < 2 || fd >= MAX_FD)
    return NULL;

  struct thread *cur = thread_current();
  return cur->fd_table[fd];
}

/* Allocate a fd to a file*/
int
allocate_fd(struct file *file)
{
  struct thread *cur = thread_current();
  for (int fd = 2; fd < MAX_FD; fd++)
  {
    if (cur->fd_table[fd] == NULL)
    {
      cur->fd_table[fd] = file;
      return fd;
    }
  }
  return -1; // No available fd
}
