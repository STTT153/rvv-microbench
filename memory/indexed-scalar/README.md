# Scalar indexed-load comparison

This benchmark mirrors the parent vector benchmark with scalar `ld`
instructions. Here, VL means the number of scalar `ld` instructions executed
in every outer kernel iteration.

Each scalar data element is 64 bits and every byte offset is 8-byte aligned.
All referenced data stays inside one 4 KiB-aligned page. The patterns are:

| Pattern | Index `i` byte offset |
| --- | ---: |
| `contiguous` | `(i * 8) % 4096` |
| `stride_16B` | `(i * 16) % 4096` |
| `cacheline_64B` | `(i * 64) % 4096` |
| `random_in_page` | fixed shuffled 8-byte-aligned order |

Like the vector benchmark, there is no separate short warm-up pass. Each timed
kernel itself runs for the full configured iteration count, naturally reaching
steady state. Every measured repeat alternates the kernel order and computes:

```text
(scalar_load_time - scalar_baseline_time) / iterations
```

The result is the median paired difference. It is the time for a group of `vl`
scalar loads; divide it by VL to obtain time per scalar `ld`.

The baseline contains the same outer/inner loops, 32-bit index loads, address
calculations, final sink store, and fence. It omits only the target `ld`.

Build and run:

```sh
make
./scalar_load --vl 16 --pattern contiguous --iterations 200000 --repeats 7
```

Without `--vl`, the default range is 1..32 for direct comparison with the A100
`e32,m4` vector benchmark. `--max-vl` may increase the range up to 512, the
number of aligned 64-bit elements in one 4 KiB page.
