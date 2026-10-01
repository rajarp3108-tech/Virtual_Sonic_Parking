# Linux System Programming Notes

Written notes for the topics that are demonstrated elsewhere or are too
advanced for the core build. Each section answers: what is it, why it exists,
and how it relates to this project.

---

## 1. Memory mapping - `mmap`

**What.** `mmap()` maps a file (or anonymous memory) into the process address
space. Afterwards the bytes are reachable through ordinary pointers, and the
kernel can page them in and out on demand.

```c
struct stat st;
fstat(fd, &st);
char* p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
if (p == MAP_FAILED) { perror("mmap"); return 1; }
/* p[0] .. p[st.st_size-1] now hold the file contents */
munmap(p, st.st_size);
```

**Why it exists.** Reading a file with `read()` copies bytes into a buffer,
which costs memory and a copy. `mmap` avoids the copy: the page cache is
mapped straight into the address space. It is the basis of shared memory, of
every memory-mapped database, and of demand-paged executables.

**Flags that matter.** `PROT_READ/WRITE`, `MAP_SHARED` (changes are written
back to the file) versus `MAP_PRIVATE` (copy-on-write, changes are private),
`MAP_ANONYMOUS` (no file, a malloc replacement), and a page-aligned offset
(`st.st_size - (st.st_size % sysconf(_SC_PAGESIZE))` for a portable length).

**In this project.** Not used in the core, and the reason is worth stating: the
telemetry is 16 bytes, so `read()` into a stack struct is strictly better than
mapping anything. `mmap` would be the right tool if the log file were mapped
and searched in place.

---

## 2. File locking

**What.** Two processes writing the same file can interleave. A lock protocol
stops them.

| API | Style | Notes |
|---|---|---|
| `fcntl(F_SETLK)` | POSIX record locks | locks are dropped when *any* fd to the file is closed in the process |
| `flock(LOCK_EX)` | BSD whole-file locks | simple; the lock is on the open file description |
| `lockf()` | POSIX, offset based | an `fcntl` wrapper |

```c
struct flock fl = {0};
fl.l_type = F_WRLCK; fl.l_whence = SEEK_SET; fl.l_start = 0; fl.l_len = 0;
while (fcntl(fd, F_SETLKW, &fl) == -1 && errno == EINTR) { /* retry */ }
```

`F_SETLK` returns immediately; `F_SETLKW` blocks. Always retry on `EINTR`, or a
signal will silently lose the lock.

**In this project.** The driver serialises its own state with a mutex, and
only one process at a time holds the device. A production version that let
several monitors log to one file would need an advisory lock around each write
- `std::ofstream` has no built-in locking. A single `flock` on the log file at
start-up would be enough for this design.

---

## 3. Asynchronous I/O

**What.** Doing I/O without blocking the calling thread.

| Method | Model | Where used |
|---|---|---|
| `select()` | poll many fds, oldest | portable, has an `FD_SETSIZE` limit |
| `poll()` / `ppoll()` | poll many fds | simpler than select, no size limit |
| `epoll` (Linux) | registered interest list | thousands of fds, no linear scan |
| `io_uring` (Linux) | shared completion ring | newest, lowest overhead |
| `AIO` (POSIX) | `aio_read`, `aio_write` | glibc implements it with threads |

All of them need the same three things: **readiness** (is there data?), **one
operation in flight per fd** (partial reads are normal and must be handled),
and **edge versus level triggering** (epoll only).

**In this project.** The monitor never waits for readiness: the driver pushes a
value on a timer, and the application polls. That is a legitimate design for
one value, and it is why the code is small. If the same design had to serve
many clients, the accept loop would move to `epoll` and the logger would stay
where it is.

---

## 4. System V signals versus POSIX signals

| Feature | System V (`signal`) | POSIX (`sigaction`) |
|---|---|---|
| Handler | `void (*)(int)` | `struct sigaction`, plus `siginfo_t*` |
| Restart behaviour | unspecified | explicit via `SA_RESTART` |
| Mask while handling | implementation defined | explicit via `sa_mask` |
| Queued signals | one pending signal per type | real-time signals queue up |
| Alt stack | none | `sigaltstack()` |
| Which handler fires | unspecified if several are set | explicit, one call decides |

```c
struct sigaction sa;
memset(&sa, 0, sizeof(sa));
sa.sa_handler = handler;
sigemptyset(&sa.sa_mask);
sa.sa_flags = 0;                 /* no SA_RESTART on purpose */
sigaction(SIGINT, &sa, NULL);
```

The classic bug: `signal()` may reset the handler to `SIG_DFL` after the first
delivery, and it may or may not restart interrupted calls. `sigaction()` has
neither problem.

**In this project.** `app/src/main.cpp` uses `sigaction` and deliberately does
**not** set `SA_RESTART`, so a blocked `read()` or `accept()` returns `EINTR`
and the loop notices the stop flag promptly. It also ignores `SIGPIPE`, and
the TCP send uses `MSG_NOSIGNAL` - so a client that vanishes cannot kill the
monitor.

---

## 5. Input/output completion concepts

Two different words, two different meanings, and students mix them up.

**Completion notification** - telling the caller that an operation finished.
Completion is reported in three classic ways:

1. **Polling** - the caller checks a status field. Simplest, wastes CPU.
2. **Interrupt driven** - the device raises an interrupt, the top half acks it
   and the bottom half (tasklet, workqueue, threaded IRQ) does the work.
3. **Completion queue / event** - the kernel posts an event and the caller
   blocks on it, or gets `epoll`/`io_uring` completion entries.

`io_uring` is the modern example: the submission queue and the completion
queue are both memory-mapped ring buffers, so even the notification avoids a
syscall.

**Why it matters here.** The driver's timer callback *is* a completion
notification, in the simplest possible form: something happened, do the work.
The rest of the design (bottom half, workqueue) is what you would add if the
work were too long to run in interrupt context.

---

## 6. Custom system call anatomy

A system call is the only way user space asks the kernel to do something.

```
user space                          kernel space
-------------                       ------------
mov  $1, %eax          syscall nr   --
mov  $fd, %edi                     --
mov  $buf, %rsi                     --
mov  $len, %edx                     --
syscall                             --> entry_SYSCALL_64 saves registers
                                            -> validates arguments
                                            -> runs the kernel function
                                            -> copies results back
                                     <-- iretq restores user space
```

Read a real one:

```bash
objdump -d --start-address=0xffffffff81000000 /boot/vmlinux | head
grep -w sys_read /boot/System.map
cat /proc/kallsyms | grep sys_read
strace -e read ./build/parking_monitor --simulate --iterations 2
```

**Writing one by hand** means four edits, and this is the honest part of the
exercise:

1. add a table entry in `arch/x86/entry/syscalls/syscall_64.c`:
   `SYSCALL_DEFINE3(my_sensor_read, int, char __user *, size_t, unsigned int);`
2. add the prototype to `include/linux/syscalls.h`
3. implement it in `drivers/...` with `copy_from_user` / `copy_to_user`
4. rebuild the kernel - **not** a module. A syscall cannot be added by a
   loadable module, because the syscall table is fixed at boot.

That last point is the reason the project does not need one: a device file
gives user space everything a custom syscall would, and it can be updated by
`rmmod`/`insmod` instead of a reboot.

---

## 7. Kernel versus user space summary

| | Kernel space | User space |
|---|---|---|
| Runs as | root, all privileges | the invoking user |
| Address space | shared, mapped in every process | private per process |
| Memory | cannot use the process heap; `kmalloc`/`vmalloc` | `malloc`/`new`, `free`/`delete` |
| Pointers | user pointers are invalid | kernel pointers are invalid |
| Errors | `ERR_PTR`, negative return codes | exceptions, `-1` + `errno` |
| Preemption | preemptible kernel code; may be interrupted | always preemptible |
| Loaded by | the bootloader, built in | `execve`, or `insmod` for a module |

A driver that writes to a user pointer directly is a security bug and a crash
waiting to happen. `copy_to_user` and `copy_from_user` exist precisely so the
kernel validates the address before touching it.

---

## 8. Daemon checklist

A correctly written daemon, in order:

1. `fork()`, parent exits - the shell gets its prompt back.
2. `setsid()` - new session, no controlling terminal.
3. `fork()` again - the session leader can never re-acquire a terminal.
4. `umask(022)`.
5. `chdir("/")` so the process does not pin a mount point.
6. Redirect 0 to `/dev/null`, 1 and 2 to the log file.
7. Optionally write a pid file and remove it on exit.
8. Handle `SIGHUP` (ignore it) if sessions are ever used.

`app/src/main.cpp` does steps 1-6. Steps 7 and 8 are omitted on purpose, and
the report should say so.
