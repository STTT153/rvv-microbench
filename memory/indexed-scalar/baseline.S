    .section .text
    .align  2

    # void scalar_baseline_kernel(const uint64_t *data,
    #                             const uint32_t *indices,
    #                             size_t vl, size_t iterations,
    #                             uint64_t *sink);
    #
    # Match scalar_load_kernel except for the omitted target ld instruction.
    .globl  scalar_baseline_kernel
    .type   scalar_baseline_kernel, @function
scalar_baseline_kernel:
    li      t4, 0
1:
    mv      t0, a1
    mv      t1, a2
2:
    lwu     t2, 0(t0)
    add     t3, a0, t2
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, 2b
    addi    a3, a3, -1
    bnez    a3, 1b

    sd      t4, 0(a4)
    fence   rw, rw
    ret
    .size   scalar_baseline_kernel, .-scalar_baseline_kernel

    .section .note.GNU-stack,"",@progbits
