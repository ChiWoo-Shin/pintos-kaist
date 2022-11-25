#include <stdio.h>
#include <stdbool.h>
#include "threads/synch.h"
#include "filesys/file.h"
#include "threads/thread.h"

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_add (void *add);
void halt_handler (void);
void exit_handler (int status);
tid_t fork_handler (const char *thread_name, struct intr_frame *f);
int exec_handler (const char *file);
bool create_handler (const char *file, unsigned initial_size);
bool remove_handler (const char *file);
int open_handler (const char *file);
int add_file_to_FDT (struct file *file);
int file_size_handler (int fd);

static struct file *find_file_using_fd (int fd);
struct thread *get_child (int pid);

int read_handler (int fd, const void *buffer, unsigned size);
int write_handler (int fd, const void *buffer, unsigned size);
void seek_handler (int fd, unsigned position);
unsigned tell_handler (int fd);
void close_handler (int fd);
void remove_fd_in_FDT(int fd);

struct lock filesys_lock;


#endif /* userprog/syscall.h */
