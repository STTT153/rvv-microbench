# Indexed load for different VL

## Goal

Measure the steady-state cost of an eight-way-unrolled `vluxei32.v` sequence
while varying both vector length (VL) and the index access pattern.

Different `LMUL` would also be selected for different `VLMAX`. The program detects VLMAX for the
selected LMUL at runtime (Assume VLEN=256 for X100) and
tests the complete range `1..VLMAX`. For example, e32 gives
VLMAX values 8, 16, 32, and 64 for LMUL 1, 2, 4, and 8 respectively.

## Test matrix

Each row is a VL and each column is one of the following byte-offset patterns:

| Pattern | Index `i` byte offset | Locality exercised |
| --- | ---: | --- |
| `contiguous` | `(i * 4) % 4096` | Unit-stride `int32_t` data |
| `stride_16B` | `(i * 16) % 4096` | Four elements per 64-byte cache line |
| `cacheline_64B` | `(i * 64) % 4096` | One element per cache line; wraps after 64 lines |
| `random_in_page` | fixed shuffled aligned-word order | Irregular access inside the same page |

## Effect of memory system

At this stage, I don't want to cache misses, TLB misses and page fault effect the result.

So The tested data region is exactly 4 KiB and is 4 KiB aligned (64 KiB L1D).
Every offset is an aligned 32-bit byte offset in the inclusive range `(0..4092)`.
Therefore every element read by `vluxei32.v`, stays inside one 4 KiB page avoiding page-crossing or TLB effects.

The programmes iterate thousands of times so the l1d cache miss rate is less than 0.1% this is futher
verified use `perf`.

## Method

For every matrix cell, the program:

1. builds the selected index vector in memory;
2. measures `baseline_kernel` and `indexed_load_kernel` as a pair with a
   per-thread `perf_event_open` hardware CPU-cycle counter;
3. repeats the pair for the configured repeat count, alternating which kernel
   runs first;
4. computes `(indexed_cycles - baseline_cycles) / (iterations * 8)` to obtain
   cycles per `vluxei32.v`, then divides that value by VL to obtain cycles per
   loaded element; and
5. reports the median paired result.

For every supported LMUL, the corresponding assembly kernel and baseline have
the same ABI, vector setup, index load, scalar loop, sink store, and fence. The
indexed loop contains eight `vluxei32.v` instructions that are absent from its
baseline. They are distributed over as many non-overlapping, LMUL-aligned
destination register groups as possible.

An example kernel for LMUL=1

```asm
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
```

## Build and run

The benchmark is intended to be compiled and run natively on a RISC-V Linux
machine with the vector extension:

```sh
make
./indexed_load > result.csv
```

The default iteration count is 100000, the default repeat count is 5, and the
default LMUL is 4. They and the maximum VL can be changed at runtime:

```sh
./indexed_load --iterations 200000 --repeats 7 --max-vl 32 > result.csv
```

Use `--lmul` to select LMUL 1, 2, 4, or 8. Without `--vl` or `--max-vl`, the
program runs every VL from 1 through VLMAX for that LMUL:

```sh
./indexed_load --lmul 2 --pattern contiguous
```

Use `--vl` to run exactly one VL, which is useful for wrapping a single test
point with `perf stat`:

```sh
./indexed_load --vl 16 --pattern contiguous --iterations 200000 --repeats 7
```

When `--vl` is used without `--lmul`, LMUL remains at its default value of 4.
When they are used together, the specified VL is checked against VLMAX for the
selected LMUL:

```sh
./indexed_load --lmul 1 --vl 8 --pattern contiguous
```

`--vl` and `--max-vl` are mutually exclusive. Both are checked against VLMAX
for the selected LMUL. `--max-vl` remains available for running a prefix of the
full `1..VLMAX` range.

Pass `--pattern` to run only one pattern and emit only that CSV column:

```sh
./indexed_load --pattern contiguous > contiguous.csv
./indexed_load --pattern random_in_page --iterations 200000 --repeats 7 > random.csv
```

Valid names are `contiguous`, `stride_16B`, `cacheline_64B`, and
`random_in_page`. Run `./indexed_load --help` for the option summary.

The same selection can be passed through the Makefile run target:

```sh
make run RUN_ARGS="--pattern contiguous --iterations 200000 --repeats 7"
```

The scalar comparison is under the sibling directory `../indexed-scalar/`.
Build it from this directory with `make scalar`, or run it with:

```sh
make scalar-run RUN_ARGS="--vl 16 --pattern contiguous --iterations 200000 --repeats 7"
```

With `--vl 16 --pattern contiguous`, the output contains both normalized
metrics for that test point:

```text
CPU = 3
LMUL = 4
vl,contiguous_difference_cycles_per_vluxei,contiguous_difference_cycles_per_element
16,...,...
```

The first metric divides by the `iterations * 8` dynamically executed vector
indexed-load instructions. The second divides once more by VL. The perf event
excludes kernel and hypervisor cycles and scales the count if the event was
multiplexed. Measurement noise can occasionally produce a small negative
baseline-subtracted result; it is not clamped.

## Reference environment

- Compiler: Ubuntu clang 21.1.8 (6ubuntu1)
- Target: `riscv64-unknown-linux-gnu`
- Flags: `-O2 -march=rv64gcv`
- Machine: K3
  - SpacemiT A100: VLEN 256, DLEN 128
  - SpacemiT X100: VLEN 1024, DLEN 1024 (to be confirmed)
- Cache:
  - L1d: 1 MiB total (16 × 64 KiB) 9 (assume 64B cahce line)
  - L1i: 1 MiB total (16 × 64 KiB)
  - L2: 10 MiB total (4 × 2560 KiB)

## Experiment results

The results below were collected with the earlier single-load,
`clock_gettime`-based implementation. They are retained as historical data and
are not directly comparable with the current eight-way perf-event results.

| Parameter | Value |
| --- | ---: |
| CPU | 3 |

All measurements below are baseline-subtracted `difference_ns` values.

### LMUL = 1

| VL | contiguous (ns) | stride 16B (ns) | cacheline 64B (ns) | random in page (ns) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 48.3899 | 48.4078 | 48.2941 | 48.2933 |
| 2 | 42.5448 | 42.3648 | 42.3890 | 42.5015 |
| 3 | 36.5597 | 36.4226 | 36.5939 | 36.4139 |
| 4 | 30.5042 | 30.9150 | 30.5009 | 30.4954 |
| 5 | 24.6599 | 24.5878 | 24.5445 | 24.6520 |
| 6 | 18.6357 | 18.6923 | 18.6357 | 18.8265 |
| 7 | 12.7264 | 12.7268 | 12.7631 | 12.7268 |
| 8 | 6.8176 | 6.8180 | 6.8175 | 6.8176 |

### LMUL = 2

| VL | contiguous (ns) | stride 16B (ns) | cacheline 64B (ns) | random in page (ns) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 77.1421 | 78.0516 | 78.1741 | 78.2079 |
| 2 | 90.5381 | 72.0736 | 71.9291 | 72.2566 |
| 3 | 84.5571 | 66.1794 | 66.0365 | 66.1098 |
| 4 | 78.6495 | 60.3126 | 60.2389 | 60.1918 |
| 5 | 72.5515 | 54.3217 | 54.3329 | 54.1567 |
| 6 | 66.7518 | 48.3087 | 48.2553 | 48.3828 |
| 7 | 60.8663 | 42.3577 | 42.4490 | 42.4136 |
| 8 | 54.7958 | 36.4126 | 36.5089 | 36.5989 |
| 9 | 51.8960 | 33.8194 | 33.7832 | 33.8224 |
| 10 | 46.1405 | 30.9488 | 31.0133 | 31.1975 |
| 11 | 40.3674 | 30.9858 | 30.9538 | 30.9871 |
| 12 | 34.3219 | 30.9396 | 30.9646 | 30.9375 |
| 13 | 28.2285 | 25.0482 | 25.1024 | 25.1682 |
| 14 | 22.3317 | 19.1619 | 19.0902 | 19.1373 |
| 15 | 16.3629 | 13.1814 | 13.1810 | 13.1814 |
| 16 | 10.4819 | 10.4540 | 10.4540 | 10.4540 |

### LMUL = 4

| VL | contiguous (ns) | stride 16B (ns) | cacheline 64B (ns) | random in page (ns) |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 154.2297 | 157.8565 | 157.9482 | 157.8315 |
| 2 | 192.7622 | 177.5292 | 177.5309 | 177.7676 |
| 3 | 186.7375 | 171.4354 | 171.4808 | 171.8100 |
| 4 | 180.6361 | 165.6865 | 165.5128 | 165.5332 |
| 5 | 174.7031 | 159.7785 | 159.6148 | 159.7539 |
| 6 | 168.8447 | 153.7334 | 153.7426 | 153.8659 |
| 7 | 162.8342 | 147.9200 | 147.8154 | 147.6196 |
| 8 | 156.5429 | 141.5270 | 141.8266 | 141.4974 |
| 9 | 154.0042 | 139.0288 | 138.9255 | 138.7526 |
| 10 | 147.9908 | 132.9675 | 132.8975 | 132.8816 |
| 11 | 142.1753 | 132.9871 | 132.8470 | 132.8604 |
| 12 | 136.0856 | 133.1799 | 132.9625 | 132.8929 |
| 13 | 130.5384 | 127.1032 | 126.9811 | 126.9907 |
| 14 | 124.2613 | 121.1173 | 121.0481 | 121.0431 |
| 15 | 118.3224 | 115.1722 | 115.1284 | 115.1634 |
| 16 | 112.4219 | 112.4223 | 112.5444 | 112.4181 |
| 17 | 106.5339 | 106.5443 | 106.5876 | 106.4797 |
| 18 | 100.5825 | 100.5525 | 100.6242 | 100.6084 |
| 19 | 94.8078 | 94.6716 | 94.6454 | 94.6333 |
| 20 | 88.7994 | 88.8052 | 88.8465 | 88.7219 |
| 21 | 82.8168 | 82.8343 | 82.9935 | 82.8585 |
| 22 | 77.0405 | 76.8751 | 76.9351 | 77.2284 |
| 23 | 71.0495 | 71.0670 | 71.0137 | 71.0620 |
| 24 | 65.1169 | 65.0923 | 65.0619 | 65.0461 |
| 25 | 59.1997 | 59.1677 | 59.1543 | 59.2664 |
| 26 | 53.2242 | 53.2305 | 53.2442 | 53.2746 |
| 27 | 47.3129 | 47.3096 | 47.3554 | 47.3504 |
| 28 | 41.4811 | 41.4078 | 41.4899 | 41.4015 |
| 29 | 35.4935 | 35.4810 | 35.4952 | 35.4868 |
| 30 | 29.8321 | 29.5909 | 29.6142 | 29.5705 |
| 31 | 23.7029 | 23.6625 | 23.6825 | 23.6746 |
| 32 | 17.7265 | 17.7549 | 17.7261 | 17.7265 |
