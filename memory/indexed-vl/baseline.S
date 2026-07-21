    .section .text
    .align  2

    # void baseline_kernel(const uint32_t *data,
    #                      const uint32_t *indices,
    #                      size_t vl, size_t iterations,
    #                      uint32_t *sink);
    #
    # Match indexed_load_kernel except for the omitted vluxei32.v.
    .globl  baseline_kernel
    .type   baseline_kernel, @function
baseline_kernel:
    vsetvli t0, a2, e32, m4, ta, ma
    vle32.v v0, (a1)

1:
    addi    a3, a3, -1
    bnez    a3, 1b

    vse32.v v8, (a4)
    fence   rw, rw
    ret
    .size   baseline_kernel, .-baseline_kernel

    .section .note.GNU-stack,"",@progbits
