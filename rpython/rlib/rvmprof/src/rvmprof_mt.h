/* Support for multithreaded write() operations */

#include <unistd.h>
#include <sys/mman.h>
#include <string.h>

#define SINGLE_BUF_SIZE  (8192 - 2 * sizeof(unsigned int))
#define MAX_NUM_BUFFERS  32

#if defined(__i386__) || defined(__amd64__)
  static inline void write_fence(void) { asm("" : : : "memory"); }
#else
  static inline void write_fence(void) { __sync_synchronize(); }
#endif


#define PROFBUF_UNUSED   0
#define PROFBUF_FILLING  1
#define PROFBUF_READY    2


struct profbuf_s {
    unsigned int data_size;
    unsigned int data_offset;
    char data[SINGLE_BUF_SIZE];
};

static char volatile profbuf_state[MAX_NUM_BUFFERS];
static struct profbuf_s *profbuf_all_buffers = NULL;
static int volatile profbuf_write_lock = 2;


static int prepare_concurrent_bufs(void)
{
    assert(sizeof(struct profbuf_s) == 8192);

    if (profbuf_all_buffers != NULL) {
        munmap(profbuf_all_buffers, sizeof(struct profbuf_s) * MAX_NUM_BUFFERS);
        profbuf_all_buffers = NULL;
    }
    profbuf_all_buffers = mmap(NULL, sizeof(struct profbuf_s) * MAX_NUM_BUFFERS,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS,
                               -1, 0);
    if (profbuf_all_buffers == MAP_FAILED) {
        profbuf_all_buffers = NULL;
        return -1;
    }
    memset((char *)profbuf_state, PROFBUF_UNUSED, sizeof(profbuf_state));
    profbuf_write_lock = 0;
    return 0;
}

static void _write_single_ready_buffer(int fd, long i)
{
    struct profbuf_s *p = &profbuf_all_buffers[i];
    ssize_t count = write(fd, p->data + p->data_offset, p->data_size);
    if (count == p->data_size) {
        profbuf_state[i] = PROFBUF_UNUSED;
    }
    else if (count > 0) {
        p->data_offset += count;
        p->data_size -= count;
    }
}

static void _write_ready_buffers(int fd)
{
    long i;
    int has_write_lock = 0;

    for (i = 0; i < MAX_NUM_BUFFERS; i++) {
        if (profbuf_state[i] == PROFBUF_READY) {
            if (!has_write_lock) {
                if (!__sync_bool_compare_and_swap(&profbuf_write_lock, 0, 1))
                    return;   /* can't acquire the write lock, give up */
                has_write_lock = 1;
            }
            _write_single_ready_buffer(fd, i);
        }
    }
    if (has_write_lock)
        profbuf_write_lock = 0;
}

static struct profbuf_s *reserve_buffer(int fd)
{
    /* Tries to enter a region of code that fills one buffer.  If
       successful, returns the profbuf_s.  It fails only if the
       concurrent buffers are all busy (extreme multithreaded usage).

       This might call write() to emit the data sitting in
       previously-prepared buffers.  In case of write() error, the
       error is ignored but unwritten data stays in the buffers.
    */
    long i;

    _write_ready_buffers(fd);

    for (i = 0; i < MAX_NUM_BUFFERS; i++) {
        if (profbuf_state[i] == PROFBUF_UNUSED &&
            __sync_bool_compare_and_swap(&profbuf_state[i], PROFBUF_UNUSED,
                                         PROFBUF_FILLING)) {
            struct profbuf_s *p = &profbuf_all_buffers[i];
            p->data_size = 0;
            p->data_offset = 0;
            return p;
        }
    }
    /* no unused buffer found */
    return NULL;
}

static void commit_buffer(int fd, struct profbuf_s *buf)
{
    /* Leaves a region of code that filled 'buf'.

       This might call write() to emit the data now ready.  In case of
       write() error, the error is ignored but unwritten data stays in
       the buffers.
    */

    /* Make sure every thread sees the full content of 'buf' */
    write_fence();

    /* Then set the 'ready' flag */
    long i = buf - profbuf_all_buffers;
    assert(profbuf_state[i] == PROFBUF_FILLING);
    profbuf_state[i] = PROFBUF_READY;

    if (!__sync_bool_compare_and_swap(&profbuf_write_lock, 0, 1)) {
        /* can't acquire the write lock, ignore */
    }
    else {
        _write_single_ready_buffer(fd, i);
        profbuf_write_lock = 0;
    }
}

static void shutdown_concurrent_bufs(int fd)
{
 retry:
    usleep(1);
    if (!__sync_bool_compare_and_swap(&profbuf_write_lock, 0, 2)) {
        /* spin loop */
        goto retry;
    }

    /* last attempt to flush buffers */
    int i;
    for (i = 0; i < MAX_NUM_BUFFERS; i++) {
        if (profbuf_state[i] == PROFBUF_READY) {
            _write_single_ready_buffer(fd, i);
        }
    }
}
