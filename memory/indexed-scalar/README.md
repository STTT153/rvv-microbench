# Scalar indexed-load comparison

This benchmark mirrors the parent vector benchmark with scalar `lwu`
instructions. The vector kernel executes eight `vluxei32.v` instructions per
outer iteration, each loading VL elements. Accordingly, the scalar kernel
executes eight `lwu` instructions for each of its VL indices, for a total of
`8 * VL` target data loads per outer iteration. This count is the same for
every LMUL at a given VL.

Each scalar data element is 32 bits and every byte offset is 4-byte aligned.
All referenced data stays inside one 4 KiB-aligned page. The patterns are:

| Pattern | Index `i` byte offset |
| --- | ---: |
| `contiguous` | `(i * 4) % 4096` |
| `stride_16B` | `(i * 16) % 4096` |
| `cacheline_64B` | `(i * 64) % 4096` |
| `random_in_page` | fixed shuffled 4-byte-aligned order |

Like the vector benchmark, there is no separate short warm-up pass. Each timed
kernel itself runs for the full configured iteration count, naturally reaching
steady state. Every measured repeat alternates the kernel order and computes:

```text
(scalar_load_time - scalar_baseline_time) / iterations
```

The result is the median paired difference. It is the time for `8 * VL`
scalar loads; divide it by `8 * VL` to obtain time per scalar `lwu`.

The baseline contains the same outer/inner loops, 32-bit index loads, address
calculations, final sink store, and fence. It omits only the eight target `lwu`
instructions issued for each index.

Build and run:

```sh
make
./scalar_load --vl 16 --pattern contiguous --iterations 200000 --repeats 7
```

Without `--vl`, the default range is 1..32 for direct comparison with the A100
`e32,m4` vector benchmark. `--max-vl` may increase the range up to 1024, the
number of aligned 32-bit elements in one 4 KiB page.
