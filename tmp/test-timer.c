#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static inline uint64_t read_cycle(void)
{
    uint64_t value;

    asm volatile(
        "fence\n\t"
        "rdcycle %0\n\t"
        : "=r"(value)
        :
        : "memory"
    );

    return value;
}

int main(void)
{
    uint64_t begin = read_cycle();

    for (volatile int i = 0; i < 1000000; ++i) {
    }

    uint64_t end = read_cycle();

    printf("begin  = %" PRIu64 "\n", begin);
    printf("end    = %" PRIu64 "\n", end);
    printf("cycles = %" PRIu64 "\n", end - begin);

    return 0;
}