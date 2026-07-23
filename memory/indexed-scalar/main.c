#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#else
#error "This benchmark requires Linux perf_event support"
#endif

typedef void (*kernel_function)(const uint32_t *data,
                                const uint32_t *indices, size_t vl,
                                size_t iterations, uint32_t *sink);

extern void scalar_load_kernel(const uint32_t *data, const uint32_t *indices,
                               size_t vl, size_t iterations, uint32_t *sink);
extern void scalar_baseline_kernel(const uint32_t *data,
                                   const uint32_t *indices, size_t vl,
                                   size_t iterations, uint32_t *sink);

enum pattern {
    PATTERN_CONTIGUOUS,
    PATTERN_STRIDE_16B,
    PATTERN_CACHELINE_64B,
    PATTERN_RANDOM_IN_PAGE,
    PATTERN_COUNT
};

static const char *const pattern_names[PATTERN_COUNT] = {
    "contiguous", "stride_16B", "cacheline_64B", "random_in_page"
};

enum {
    DEFAULT_ITERATIONS = 100000,
    DEFAULT_REPEATS = 5,
    DEFAULT_MAX_VL = 32,
    SCALAR_LOADS_PER_INDEX = 8,
    TEST_PAGE_BYTES = 4096,
    ELEMENTS_PER_PAGE = TEST_PAGE_BYTES / (int)sizeof(uint32_t),
    CACHE_LINES_PER_PAGE = TEST_PAGE_BYTES / 64
};

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--pattern NAME] [--iterations N] [--repeats N]\n"
            "          [--vl N | --max-vl N]\n"
            "\n"
            "For the scalar benchmark, each outer kernel iteration executes\n"
            "eight lwu instructions for each of the VL indices (8 * VL loads\n"
            "in total), matching the vector kernel's eight-way unroll. If\n"
            "--pattern is omitted, all patterns are run. Results are paired,\n"
            "baseline-subtracted CPU cycles per target lwu.\n",
            program);
}

static int compare_double(const void *left, const void *right)
{
    const double a = *(const double *)left;
    const double b = *(const double *)right;
    return (a > b) - (a < b);
}

static double median(double *values, size_t count)
{
    qsort(values, count, sizeof(*values), compare_double);
    if (count % 2 != 0)
        return values[count / 2];
    return (values[count / 2 - 1] + values[count / 2]) / 2.0;
}

struct perf_counter_value {
    uint64_t value;
    uint64_t time_enabled;
    uint64_t time_running;
};

static int open_cycle_counter(void)
{
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = PERF_COUNT_HW_CPU_CYCLES;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED |
                       PERF_FORMAT_TOTAL_TIME_RUNNING;

    int fd = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
    if (fd < 0) {
        perror("perf_event_open(CPU cycles)");
        fprintf(stderr,
                "check whether the kernel and perf_event_paranoid allow "
                "per-thread hardware counters\n");
        exit(EXIT_FAILURE);
    }
    return fd;
}

static struct perf_counter_value read_cycle_counter(int fd)
{
    struct perf_counter_value counter;
    ssize_t bytes = read(fd, &counter, sizeof(counter));
    if (bytes != (ssize_t)sizeof(counter)) {
        if (bytes < 0)
            perror("reading perf cycle counter");
        else
            fprintf(stderr, "short read from perf cycle counter\n");
        exit(EXIT_FAILURE);
    }
    return counter;
}

static struct perf_counter_value start_cycle_counter(int fd)
{
    if (ioctl(fd, PERF_EVENT_IOC_RESET, 0) != 0) {
        perror("resetting perf cycle counter");
        exit(EXIT_FAILURE);
    }
    struct perf_counter_value before = read_cycle_counter(fd);
    if (ioctl(fd, PERF_EVENT_IOC_ENABLE, 0) != 0) {
        perror("starting perf cycle counter");
        exit(EXIT_FAILURE);
    }
    return before;
}

static double stop_cycle_counter(int fd,
                                 const struct perf_counter_value *before)
{
    if (ioctl(fd, PERF_EVENT_IOC_DISABLE, 0) != 0) {
        perror("stopping perf cycle counter");
        exit(EXIT_FAILURE);
    }
    struct perf_counter_value after = read_cycle_counter(fd);
    uint64_t time_enabled = after.time_enabled - before->time_enabled;
    uint64_t time_running = after.time_running - before->time_running;
    if (time_running == 0) {
        fprintf(stderr, "perf cycle counter was never scheduled\n");
        exit(EXIT_FAILURE);
    }

    return (double)after.value * (double)time_enabled / (double)time_running;
}

static double measure_kernel_cycles(int counter_fd, kernel_function kernel,
                                    const uint32_t *data,
                                    const uint32_t *indices, size_t vl,
                                    size_t iterations, uint32_t *sink)
{
    struct perf_counter_value before = start_cycle_counter(counter_fd);
    kernel(data, indices, vl, iterations, sink);
    return stop_cycle_counter(counter_fd, &before);
}

static size_t parse_pattern(const char *text)
{
    for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
        if (strcmp(text, pattern_names[pattern]) == 0)
            return pattern;
    }
    fprintf(stderr, "unknown pattern: %s\n", text);
    fprintf(stderr, "valid patterns:");
    for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern)
        fprintf(stderr, " %s", pattern_names[pattern]);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static size_t parse_positive(const char *option, const char *text)
{
    char *end = NULL;
    errno = 0;
    uintmax_t value = strtoumax(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || text[0] == '-' || *end != '\0' ||
        value == 0 || value > SIZE_MAX) {
        fprintf(stderr, "%s expects a positive integer, got '%s'\n", option,
                text);
        exit(EXIT_FAILURE);
    }
    return (size_t)value;
}

static void *allocate_aligned(size_t alignment, size_t bytes)
{
    void *pointer = NULL;
    int error = posix_memalign(&pointer, alignment, bytes);
    if (error != 0) {
        fprintf(stderr, "posix_memalign(%zu) failed: %s\n", bytes,
                strerror(error));
        exit(EXIT_FAILURE);
    }
    return pointer;
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void make_random_element_order(uint32_t *order)
{
    for (size_t i = 0; i < ELEMENTS_PER_PAGE; ++i)
        order[i] = (uint32_t)i;

    uint32_t state = UINT32_C(0x6d2b79f5);
    for (size_t i = ELEMENTS_PER_PAGE; i > 1; --i) {
        size_t other = (size_t)(next_random(&state) % i);
        uint32_t temporary = order[i - 1];
        order[i - 1] = order[other];
        order[other] = temporary;
    }
}

static void fill_indices(uint32_t *indices,
                         const uint32_t *random_element_order, size_t vl,
                         enum pattern pattern)
{
    for (size_t i = 0; i < vl; ++i) {
        size_t offset;
        switch (pattern) {
        case PATTERN_CONTIGUOUS:
            offset = (i % ELEMENTS_PER_PAGE) * sizeof(uint32_t);
            break;
        case PATTERN_STRIDE_16B:
            offset = (i % (TEST_PAGE_BYTES / 16)) * 16;
            break;
        case PATTERN_CACHELINE_64B:
            offset = (i % CACHE_LINES_PER_PAGE) * 64;
            break;
        case PATTERN_RANDOM_IN_PAGE:
            offset =
                (size_t)random_element_order[i % ELEMENTS_PER_PAGE] *
                sizeof(uint32_t);
            break;
        default:
            abort();
        }
        if (offset > TEST_PAGE_BYTES - sizeof(uint32_t) ||
            offset % sizeof(uint32_t) != 0)
            abort();
        indices[i] = (uint32_t)offset;
    }
}

int main(int argc, char **argv)
{
    size_t iterations = DEFAULT_ITERATIONS;
    size_t repeats = DEFAULT_REPEATS;
    size_t requested_vl = 0;
    size_t requested_max_vl = 0;
    size_t selected_pattern = PATTERN_COUNT;

    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (strcmp(option, "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (strcmp(option, "--pattern") == 0)
            selected_pattern = parse_pattern(argv[++i]);
        else if (strcmp(option, "--iterations") == 0)
            iterations = parse_positive(option, argv[++i]);
        else if (strcmp(option, "--repeats") == 0)
            repeats = parse_positive(option, argv[++i]);
        else if (strcmp(option, "--vl") == 0)
            requested_vl = parse_positive(option, argv[++i]);
        else if (strcmp(option, "--max-vl") == 0)
            requested_max_vl = parse_positive(option, argv[++i]);
        else {
            fprintf(stderr, "unknown option: %s\n", option);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (requested_vl != 0 && requested_max_vl != 0) {
        fprintf(stderr, "--vl and --max-vl cannot be used together\n");
        return EXIT_FAILURE;
    }

    size_t first_vl = requested_vl ? requested_vl : 1;
    size_t max_vl = requested_vl
                        ? requested_vl
                        : (requested_max_vl ? requested_max_vl
                                            : DEFAULT_MAX_VL);
    if (max_vl > ELEMENTS_PER_PAGE) {
        fprintf(stderr, "requested VL %zu exceeds the single-page limit %d\n",
                max_vl, ELEMENTS_PER_PAGE);
        return EXIT_FAILURE;
    }
    if (repeats > SIZE_MAX / sizeof(double)) {
        fprintf(stderr, "--repeats %zu is too large\n", repeats);
        return EXIT_FAILURE;
    }

    uint32_t *data = allocate_aligned(TEST_PAGE_BYTES, TEST_PAGE_BYTES);
    uint32_t *indices =
        allocate_aligned(TEST_PAGE_BYTES, max_vl * sizeof(*indices));
    uint32_t *sink = allocate_aligned(TEST_PAGE_BYTES, sizeof(*sink));
    uint32_t *random_element_order =
        allocate_aligned(TEST_PAGE_BYTES,
                         ELEMENTS_PER_PAGE * sizeof(*random_element_order));
    double *samples = malloc(repeats * sizeof(*samples));
    if (samples == NULL) {
        perror("malloc");
        return EXIT_FAILURE;
    }
    int cycle_counter_fd = open_cycle_counter();

    for (size_t i = 0; i < ELEMENTS_PER_PAGE; ++i)
        data[i] = (uint32_t)(i * sizeof(*data));
    make_random_element_order(random_element_order);

    printf("CPU = %d\n", sched_getcpu());
    printf("vl");
    for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
        if (selected_pattern != PATTERN_COUNT && pattern != selected_pattern)
            continue;
        printf(",%s_cycles_per_lwu", pattern_names[pattern]);
    }
    putchar('\n');

    for (size_t vl = first_vl; vl <= max_vl; ++vl) {
        printf("%zu", vl);
        for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
            if (selected_pattern != PATTERN_COUNT &&
                pattern != selected_pattern)
                continue;
            fill_indices(indices, random_element_order, vl,
                         (enum pattern)pattern);

            for (size_t repeat = 0; repeat < repeats; ++repeat) {
                double baseline_cycles;
                double load_cycles;

                if (repeat % 2 == 0) {
                    baseline_cycles = measure_kernel_cycles(
                        cycle_counter_fd, scalar_baseline_kernel, data,
                        indices, vl, iterations, sink);
                    load_cycles = measure_kernel_cycles(
                        cycle_counter_fd, scalar_load_kernel, data, indices,
                        vl, iterations, sink);
                } else {
                    load_cycles = measure_kernel_cycles(
                        cycle_counter_fd, scalar_load_kernel, data, indices,
                        vl, iterations, sink);
                    baseline_cycles = measure_kernel_cycles(
                        cycle_counter_fd, scalar_baseline_kernel, data,
                        indices, vl, iterations, sink);
                }

                samples[repeat] =
                    (load_cycles - baseline_cycles) /
                    ((double)iterations * SCALAR_LOADS_PER_INDEX *
                     (double)vl);
            }
            printf(",%.4f", median(samples, repeats));
        }
        putchar('\n');
    }

    close(cycle_counter_fd);
    free(samples);
    free(random_element_order);
    free(sink);
    free(indices);
    free(data);
    return EXIT_SUCCESS;
}
