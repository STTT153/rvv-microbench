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

extern size_t rvv_vlmax_e32m4(void);
extern void indexed_load_kernel(const uint32_t *data, const uint32_t *indices,
                                size_t vl, size_t iterations, uint32_t *sink);
extern void baseline_kernel(const uint32_t *data, const uint32_t *indices,
                            size_t vl, size_t iterations, uint32_t *sink);

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
    TEST_PAGE_BYTES = 4096,
    WORDS_PER_PAGE = TEST_PAGE_BYTES / (int)sizeof(uint32_t),
    CACHE_LINES_PER_PAGE = TEST_PAGE_BYTES / 64
};

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s [--pattern NAME] [--iterations N] [--max-vl N]\n"
            "\n"
            "Print a CSV matrix whose rows are VL values and whose columns\n"
            "are indexed-load access patterns. If --pattern is omitted, all\n"
            "patterns are run. Pattern names: contiguous, stride_16B,\n"
            "cacheline_64B, random_in_page. Values are baseline-subtracted\n"
            "nanoseconds per vluxei32.v instruction.\n",
            program);
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

static void make_random_word_order(uint32_t *order)
{
    for (size_t i = 0; i < WORDS_PER_PAGE; ++i)
        order[i] = (uint32_t)i;

    uint32_t state = UINT32_C(0x6d2b79f5);
    for (size_t i = WORDS_PER_PAGE; i > 1; --i) {
        size_t other = (size_t)(next_random(&state) % i);
        uint32_t temporary = order[i - 1];
        order[i - 1] = order[other];
        order[other] = temporary;
    }
}

static void fill_indices(uint32_t *indices, const uint32_t *random_word_order,
                         size_t vl, enum pattern pattern)
{
    for (size_t i = 0; i < vl; ++i) {
        size_t offset;
        switch (pattern) {
        case PATTERN_CONTIGUOUS:
            offset = (i % WORDS_PER_PAGE) * sizeof(uint32_t);
            break;
        case PATTERN_STRIDE_16B:
            offset = (i % (TEST_PAGE_BYTES / 16)) * 16;
            break;
        case PATTERN_CACHELINE_64B:
            offset = (i % CACHE_LINES_PER_PAGE) * 64;
            break;
        case PATTERN_RANDOM_IN_PAGE:
            offset = (size_t)random_word_order[i % WORDS_PER_PAGE] *
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
    size_t requested_max_vl = 0;
    size_t selected_pattern = PATTERN_COUNT;

    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (i + 1 >= argc) {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[i], "--pattern") == 0)
            selected_pattern = parse_pattern(argv[++i]);
        else if (strcmp(argv[i], "--iterations") == 0)
            iterations = parse_positive(option, argv[++i]);
        else if (strcmp(argv[i], "--max-vl") == 0)
            requested_max_vl = parse_positive(option, argv[++i]);
        else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    const size_t hardware_vlmax = rvv_vlmax_e32m4();
    size_t max_vl = requested_max_vl ? requested_max_vl : hardware_vlmax;
    if (max_vl > hardware_vlmax) {
        fprintf(stderr, "--max-vl %zu exceeds hardware VLMAX %zu\n", max_vl,
                hardware_vlmax);
        return EXIT_FAILURE;
    }
    if (max_vl > WORDS_PER_PAGE) {
        fprintf(stderr,
                "VLMAX %zu exceeds the single-page limit of %d e32 lanes\n",
                max_vl, WORDS_PER_PAGE);
        return EXIT_FAILURE;
    }

    // allocate test data
    const size_t data_bytes = TEST_PAGE_BYTES;
    uint32_t *data = allocate_aligned(TEST_PAGE_BYTES, data_bytes);
    uint32_t *indices =
        allocate_aligned(TEST_PAGE_BYTES, max_vl * sizeof(*indices));
    uint32_t *sink =
        allocate_aligned(TEST_PAGE_BYTES, max_vl * sizeof(*sink));
    uint32_t *random_word_order =
        allocate_aligned(TEST_PAGE_BYTES,
                         WORDS_PER_PAGE * sizeof(*random_word_order));

    for (size_t i = 0; i < data_bytes / sizeof(*data); ++i)
        data[i] = (uint32_t)(i * UINT32_C(2654435761));
    make_random_word_order(random_word_order);

    printf("CPU = %d\n", sched_getcpu());
    printf("vl");
    for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
        if (selected_pattern != PATTERN_COUNT && pattern != selected_pattern)
            continue;
        printf(",%s", pattern_names[pattern]);
    }
    putchar('\n');

    // test on vl from 1..VLMAX
    for (size_t vl = 1; vl <= max_vl; ++vl) {
        printf("%zu", vl);
        // test against patterns
        for (size_t pattern = 0; pattern < PATTERN_COUNT; ++pattern) {
            if (selected_pattern != PATTERN_COUNT &&
                pattern != selected_pattern)
                continue;
            fill_indices(indices, random_word_order, vl, (enum pattern)pattern);

            uint64_t start = monotonic_time_ns();
            baseline_kernel(data, indices, vl, iterations, sink);
            uint64_t baseline_elapsed = monotonic_time_ns() - start;

            start = monotonic_time_ns();
            indexed_load_kernel(data, indices, vl, iterations, sink);
            uint64_t indexed_elapsed = monotonic_time_ns() - start;

            double adjusted_nanoseconds =
                ((double)indexed_elapsed - (double)baseline_elapsed) /
                (double)iterations;
            printf(",%.4f", adjusted_nanoseconds);
        }
        putchar('\n');
    }

    free(random_word_order);
    free(sink);
    free(indices);
    free(data);
    return EXIT_SUCCESS;
}
