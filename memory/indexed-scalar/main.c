#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <sched.h>
#else
static int sched_getcpu(void)
{
    return -1;
}
#endif

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
            "--pattern is omitted, all patterns are run.\n",
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

static uint64_t monotonic_time_ns(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
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

    for (size_t i = 0; i < ELEMENTS_PER_PAGE; ++i)
        data[i] = (uint32_t)(i * sizeof(*data));
    make_random_element_order(random_element_order);

    printf("CPU = %d\n", sched_getcpu());
    printf("vl");
    for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
        if (selected_pattern != PATTERN_COUNT && pattern != selected_pattern)
            continue;
        printf(",%s_difference_ns", pattern_names[pattern]);
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
                uint64_t baseline_elapsed;
                uint64_t load_elapsed;

                if (repeat % 2 == 0) {
                    uint64_t start = monotonic_time_ns();
                    scalar_baseline_kernel(data, indices, vl, iterations,
                                           sink);
                    baseline_elapsed = monotonic_time_ns() - start;

                    start = monotonic_time_ns();
                    scalar_load_kernel(data, indices, vl, iterations, sink);
                    load_elapsed = monotonic_time_ns() - start;
                } else {
                    uint64_t start = monotonic_time_ns();
                    scalar_load_kernel(data, indices, vl, iterations, sink);
                    load_elapsed = monotonic_time_ns() - start;

                    start = monotonic_time_ns();
                    scalar_baseline_kernel(data, indices, vl, iterations,
                                           sink);
                    baseline_elapsed = monotonic_time_ns() - start;
                }

                samples[repeat] =
                    ((double)load_elapsed - (double)baseline_elapsed) /
                    (double)iterations;
            }
            printf(",%.4f", median(samples, repeats));
        }
        putchar('\n');
    }

    free(samples);
    free(random_element_order);
    free(sink);
    free(indices);
    free(data);
    return EXIT_SUCCESS;
}
