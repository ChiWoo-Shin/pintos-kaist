#include <stdio.h>
#include <stdbool.h>

#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_add(void *add);
void halt(void);
void exit(int status);
int exec (const char *file);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int write (int fd, const void *buffer, unsigned size);
void check_add(void *add);

#endif /* userprog/syscall.h */
