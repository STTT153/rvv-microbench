    .section .text
    .align  2

    .globl  rvv_vlmax_e32m1
    .type   rvv_vlmax_e32m1, @function
rvv_vlmax_e32m1:
    li      a0, -1
    vsetvli a0, a0, e32, m1, ta, ma
    ret
    .size   rvv_vlmax_e32m1, .-rvv_vlmax_e32m1

    .globl  rvv_vlmax_e32m2
    .type   rvv_vlmax_e32m2, @function
rvv_vlmax_e32m2:
    li      a0, -1
    vsetvli a0, a0, e32, m2, ta, ma
    ret
    .size   rvv_vlmax_e32m2, .-rvv_vlmax_e32m2

    .globl  rvv_vlmax_e32m4
    .type   rvv_vlmax_e32m4, @function
rvv_vlmax_e32m4:
    li      a0, -1
    vsetvli a0, a0, e32, m4, ta, ma
    ret
    .size   rvv_vlmax_e32m4, .-rvv_vlmax_e32m4

    .globl  rvv_vlmax_e32m8
    .type   rvv_vlmax_e32m8, @function
rvv_vlmax_e32m8:
    li      a0, -1
    vsetvli a0, a0, e32, m8, ta, ma
    ret
    .size   rvv_vlmax_e32m8, .-rvv_vlmax_e32m8

    # Each kernel has this ABI:
    # void indexed_load_kernel_mX(const uint32_t *data,
    #                             const uint32_t *indices,
    #                             size_t vl, size_t iterations,
    #                             uint32_t *sink);
    #
    # v0 starts the index register group.  Each loop iteration contains eight
    # indexed loads, distributed over as many non-overlapping, LMUL-aligned
    # destination register groups as possible.  This avoids an unnecessary
    # destination dependency when more than one register group is available.

    .globl  indexed_load_kernel_m1
    .type   indexed_load_kernel_m1, @function
indexed_load_kernel_m1:
    vsetvli t0, a2, e32, m1, ta, ma
    vle32.v v0, (a1)
1:
    vluxei32.v v8, (a0), v0
    vluxei32.v v9, (a0), v0
    vluxei32.v v10, (a0), v0
    vluxei32.v v11, (a0), v0
    vluxei32.v v12, (a0), v0
    vluxei32.v v13, (a0), v0
    vluxei32.v v14, (a0), v0
    vluxei32.v v15, (a0), v0
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v15, (a4)
    fence   rw, rw
    ret
    .size   indexed_load_kernel_m1, .-indexed_load_kernel_m1

    .globl  indexed_load_kernel_m2
    .type   indexed_load_kernel_m2, @function
indexed_load_kernel_m2:
    vsetvli t0, a2, e32, m2, ta, ma
    vle32.v v0, (a1)
1:
    vluxei32.v v2, (a0), v0
    vluxei32.v v4, (a0), v0
    vluxei32.v v6, (a0), v0
    vluxei32.v v8, (a0), v0
    vluxei32.v v10, (a0), v0
    vluxei32.v v12, (a0), v0
    vluxei32.v v14, (a0), v0
    vluxei32.v v16, (a0), v0
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v16, (a4)
    fence   rw, rw
    ret
    .size   indexed_load_kernel_m2, .-indexed_load_kernel_m2

    .globl  indexed_load_kernel_m4
    .type   indexed_load_kernel_m4, @function
indexed_load_kernel_m4:
    vsetvli t0, a2, e32, m4, ta, ma
    vle32.v v0, (a1)
1:
    vluxei32.v v4, (a0), v0
    vluxei32.v v8, (a0), v0
    vluxei32.v v12, (a0), v0
    vluxei32.v v16, (a0), v0
    vluxei32.v v20, (a0), v0
    vluxei32.v v24, (a0), v0
    vluxei32.v v28, (a0), v0
    vluxei32.v v4, (a0), v0
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v4, (a4)
    fence   rw, rw
    ret
    .size   indexed_load_kernel_m4, .-indexed_load_kernel_m4

    .globl  indexed_load_kernel_m8
    .type   indexed_load_kernel_m8, @function
indexed_load_kernel_m8:
    vsetvli t0, a2, e32, m8, ta, ma
    vle32.v v0, (a1)
1:
    vluxei32.v v8, (a0), v0
    vluxei32.v v16, (a0), v0
    vluxei32.v v24, (a0), v0
    vluxei32.v v8, (a0), v0
    vluxei32.v v16, (a0), v0
    vluxei32.v v24, (a0), v0
    vluxei32.v v8, (a0), v0
    vluxei32.v v16, (a0), v0
    addi    a3, a3, -1
    bnez    a3, 1b
    vse32.v v16, (a4)
    fence   rw, rw
    ret
    .size   indexed_load_kernel_m8, .-indexed_load_kernel_m8

    .section .note.GNU-stack,"",@progbits
