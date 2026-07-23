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

Like the vector benchmark, there is no separate short warm-up pass. Each
kernel runs for the full configured iteration count. `--cycel` (the default)
uses a per-thread `perf_event_open` CPU-cycle counter; `--wallclock` uses
`clock_gettime(CLOCK_MONOTONIC)`. Every paired repeat alternates the kernel
order and computes:

```text
(scalar_load_cycles - scalar_baseline_cycles) / (iterations * 8 * VL)
```

The reported result is therefore the median paired, baseline-subtracted cycles
or nanoseconds per target scalar `lwu`.

The baseline contains the same outer/inner loops, 32-bit index loads, address
calculations, final sink store, and fence. It omits only the eight target `lwu`
instructions issued for each index.

Build and run:

```sh
make
./scalar_load --vl 16 --pattern contiguous --iterations 200000 --repeats 7
```

For external perf-event measurement, run baseline and target separately with
wallclock timing so the program does not compete for hardware counters:

```sh
perf stat -e L1-dcache-loads:u,L1-dcache-load-misses:u \
  taskset -c 3 ./scalar_load --wallclock --kernel baseline \
  --vl 4 --pattern contiguous --repeats 1

perf stat -e L1-dcache-loads:u,L1-dcache-load-misses:u \
  taskset -c 3 ./scalar_load --wallclock --kernel target \
  --vl 4 --pattern contiguous --repeats 1
```

`--kernel paired` is the default and outputs only the normalized difference.
`--cycel` and `--wallclock` are mutually exclusive; `--cycle` is accepted as
an alias for `--cycel`.

Without `--vl`, the default range is 1..32 for direct comparison with the A100
`e32,m4` vector benchmark. `--max-vl` may increase the range up to 1024, the
number of aligned 32-bit elements in one 4 KiB page.
