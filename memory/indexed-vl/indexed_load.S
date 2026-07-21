    .section .text
    .align  2

    .globl  rvv_vlmax_e32m4
    .type   rvv_vlmax_e32m4, @function
rvv_vlmax_e32m4:
    li      a0, -1
    vsetvli a0, a0, e32, m4, ta, ma
    ret
    .size   rvv_vlmax_e32m4, .-rvv_vlmax_e32m4

    # uint64_t indexed_load_kernel(const uint32_t *data,
    #                              const uint32_t *indices,
    #                              size_t vl, size_t iterations,
    #                              uint32_t *sink);
    #
    # v0-v3  hold 32-bit byte offsets (LMUL=4).
    # v8-v11 hold the loaded 32-bit elements (LMUL=4).
    # The measured loop contains exactly one vector memory instruction.
    .globl  indexed_load_kernel
    .type   indexed_load_kernel, @function
indexed_load_kernel:
    vsetvli t0, a2, e32, m4, ta, ma
    vle32.v v0, (a1)

    # Keep setup and the timed memory operations on their intended sides of
    # the cycle-counter reads.
    fence   rw, rw
    rdcycle t1
1:
    vluxei32.v v8, (a0), v0
    addi    a3, a3, -1
    bnez    a3, 1b
    fence   rw, rw
    rdcycle t2

    # Make the final load architecturally observable, outside the timed area.
    vse32.v v8, (a4)
    sub     a0, t2, t1
    ret
    .size   indexed_load_kernel, .-indexed_load_kernel

    .section .note.GNU-stack,"",@progbits
