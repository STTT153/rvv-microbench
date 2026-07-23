    .section .text
    .align  2

    # void scalar_load_kernel(const uint32_t *data,
    #                         const uint32_t *indices,
    #                         size_t vl, size_t iterations,
    #                         uint32_t *sink);
    #
    # Each outer iteration executes exactly 8 * vl target lwu instructions,
    # matching the eight vluxei32.v instructions in each vector-kernel
    # iteration.  The index load and address calculation are also present in
    # scalar_baseline.
    .globl  scalar_load_kernel
    .type   scalar_load_kernel, @function
scalar_load_kernel:
    li      t5, 0
1:
    mv      t0, a1
    mv      t1, a2
2:
    lwu     t2, 0(t0)
    add     t3, a0, t2
    lwu     t4, 0(t3)
    lwu     t5, 0(t3)
    lwu     t6, 0(t3)
    lwu     a5, 0(t3)
    lwu     a6, 0(t3)
    lwu     a7, 0(t3)
    lwu     t4, 0(t3)
    lwu     t5, 0(t3)
    addi    t0, t0, 4
    addi    t1, t1, -1
    bnez    t1, 2b
    addi    a3, a3, -1
    bnez    a3, 1b

    sw      t5, 0(a4)
    fence   rw, rw
    ret
    .size   scalar_load_kernel, .-scalar_load_kernel

    .section .note.GNU-stack,"",@progbits
