/*
 * Copyright (c) 2025 corpus.core
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/unistd.h>

/* Semihosting operations */
#define SYS_WRITE 0x05
#define SYS_READ  0x06
#define SYS_OPEN  0x01
#define SYS_CLOSE 0x02
#define SYS_FLEN  0x0C
#define SYS_SEEK  0x0A
#define SYS_EXIT  0x18

extern uint32_t __attribute__((naked)) semihosting_call(uint32_t operation, void* args) {
  __asm volatile(
      "mov r0, %[operation] \n"
      "mov r1, %[args] \n"
      "bkpt 0xAB \n"
      "bx lr \n"
      : : [operation] "r"(operation), [args] "r"(args) : "r0", "r1");
}

/* Register name faking - works in collusion with the linker script */
extern uint8_t  end asm("end"); /* Symbol defined in linker script */
static uint8_t* heap_end;

/* Dynamic memory allocation related syscalls */
void* _sbrk(ptrdiff_t incr) {
  static uint8_t* heap = NULL;
  uint8_t*        prev_heap;

  if (heap == NULL) {
    heap = &end;
  }
  prev_heap = heap;

  /* Check if we will exceed the heap end */
  if (heap + incr > heap_end) {
    /* Initialize heap_end if it hasn't been set yet */
    if (heap_end == NULL) {
      heap_end = &end + 0x1000; /* Allocate 4KB heap by default */
    }
    else {
      errno = ENOMEM;
      return (void*) -1;
    }
  }

  heap += incr;
  return prev_heap;
}

/* File operations - implemented through semihosting */
int _open(const char* name, int mode) {
  uint32_t args[3] = {(uint32_t) name, mode, strlen(name)};
  return semihosting_call(SYS_OPEN, args);
}

int _close(int file) {
  uint32_t args[1] = {file};
  return semihosting_call(SYS_CLOSE, args);
}

int _read(int file, char* ptr, int len) {
  uint32_t args[3] = {file, (uint32_t) ptr, len};
  return semihosting_call(SYS_READ, args);
}

int _write(int file, char* ptr, int len) {
  /* Use semihosting SYS_WRITE for stdout/stderr */
  if (file == STDOUT_FILENO || file == STDERR_FILENO) {
    uint32_t args[3] = {file, (uint32_t) ptr, len};
    return semihosting_call(SYS_WRITE, args);
  }
  return -1;
}

int _fstat(int file, struct stat* st) {
  /* Always report regular files */
  st->st_mode = S_IFREG;
  return 0;
}

int _isatty(int file) {
  /* Return 1 for stdout/stderr, 0 otherwise */
  return (file == STDOUT_FILENO || file == STDERR_FILENO) ? 1 : 0;
}

int _lseek(int file, int ptr, int dir) {
  uint32_t args[3] = {file, ptr, dir};
  return semihosting_call(SYS_SEEK, args);
}

/* Process-related syscalls */
int _getpid(void) {
  return 1;
}

int _kill(int pid, int sig) {
  errno = EINVAL;
  return -1;
}

void _exit(int status) {
  uint32_t args[1] = {status};
  semihosting_call(SYS_EXIT, args);
  while (1) {}
}

/* Clock implementation */
clock_t _times(struct tms* buf) {
  buf->tms_utime  = 0;
  buf->tms_stime  = 0;
  buf->tms_cutime = 0;
  buf->tms_cstime = 0;
  return 0;
}

/* Required for C++ projects */
void __cxa_pure_virtual(void) {
  while (1);
}

/* Override some of the standard C library functions */
int* __errno(void) {
  static int _errno;
  return &_errno;
}