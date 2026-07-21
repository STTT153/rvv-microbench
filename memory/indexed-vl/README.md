# Indexed load for different VL

## Goal

Measure the steady-state cost of one `vluxei32.v` while varying both vector
length (VL) and the index access pattern. The result is a two-dimensional
**VL × pattern** matrix.

The vector configuration is `e32,m4`. Consequently, the default VL range is
`1..VLMAX`, detected on the machine at runtime. For example, VLEN=256 gives a
VLMAX of 32, while VLEN=1024 gives a VLMAX of 128.

## Test matrix

Each row is a VL and each column is one of the following byte-offset patterns:

| Pattern | Index `i` byte offset | Locality exercised |
| --- | ---: | --- |
| `contiguous` | `(i * 4) % 4096` | Unit-stride `int32_t` data |
| `stride_16B` | `(i * 16) % 4096` | Four elements per 64-byte cache line |
| `cacheline_64B` | `(i * 64) % 4096` | One element per cache line; wraps after 64 lines |
| `random_in_page` | fixed shuffled aligned-word order | Irregular access inside the same page |

The tested data region is exactly 4 KiB and is 4 KiB aligned. Every offset is an
aligned 32-bit byte offset in the inclusive range `0..4092`. Therefore every
element read by `vluxei32.v`, and the complete gather instruction, stays inside
one 4 KiB page. This is stricter than the 64 KiB working-set limit and avoids
page-crossing and multi-page/TLB effects.

The index, sink, and random-order buffers are also each 4 KiB aligned and fit
within one page. The four benchmark buffers occupy at most 16 KiB in total. To
keep this invariant, the program rejects a hardware VLMAX above 1024 e32 lanes;
the target A100 and X100 VLMAX values are only 32 and 128 respectively.

The random pattern is deterministic and prefix-stable up to 1024 lanes:
increasing VL retains all earlier indices and appends more indices. Patterns
wrap inside the same page if a future implementation has a VLMAX above their
number of unique positions.

## Method

For every matrix cell, the program:

1. builds the selected index vector in memory;
2. measures `baseline_kernel` once with `clock_gettime(CLOCK_MONOTONIC)`;
3. measures `indexed_load_kernel` once with the same clock; and
4. reports `(indexed_time - baseline_time) / iterations`.

The two assembly kernels have the same ABI, vector setup, index load, scalar
loop, sink store, and fence. The only instruction present in the indexed loop
but absent from the baseline loop is `vluxei32.v v8, (a0), v0`.

There is no separate short warm-up call and each matrix cell is measured once.
The benchmark uses `clock_gettime()` rather than the inaccessible `cycle` CSR.

`v8-v11` are written to a sink after the timed region so the final indexed load
is architecturally observable. The 4 KiB data page remains resident while the
matrix cells are measured, so the matrix describes hot-L1 execution rather than
cache-capacity or page behavior. It is a steady-state instruction-cost benchmark;
independent iterations may overlap on an out-of-order implementation, so the
number should not be interpreted as dependent-use latency.

## Build and run

The benchmark is intended to be compiled and run natively on a RISC-V Linux
machine with the vector extension:

```sh
make
./indexed_load > result.csv
```

The default iteration count is 100000. It and the maximum VL can be changed at
runtime. Every matrix cell is measured exactly once:

```sh
./indexed_load --iterations 200000 --max-vl 32 > result.csv
```

Pass `--pattern` to run only one pattern and emit only that CSV column:

```sh
./indexed_load --pattern contiguous > contiguous.csv
./indexed_load --pattern random_in_page --iterations 200000 > random.csv
```

Valid names are `contiguous`, `stride_16B`, `cacheline_64B`, and
`random_in_page`. `--max-vl` may reduce the number of rows but cannot exceed
hardware VLMAX. Run `./indexed_load --help` for the option summary.

The same selection can be passed through the Makefile run target:

```sh
make run RUN_ARGS="--pattern contiguous --iterations 200000"
```

The output is directly usable as a two-dimensional CSV matrix:

```text
CPU = 3
vl,contiguous,stride_16B,cacheline_64B,random_in_page
1,3.1200,3.1300,3.1200,3.1200
2,3.1800,3.1900,3.2100,3.2300
...
```

Values are baseline-subtracted nanoseconds per `vluxei32.v` instruction, not
nanoseconds per element. Because each kernel is measured once, timer or OS
noise can occasionally produce a small negative difference; it is not clamped.

## Reference environment

- Compiler: Ubuntu clang 21.1.8 (6ubuntu1)
- Target: `riscv64-unknown-linux-gnu`
- Flags: `-O2 -march=rv64gcv`
- Machine: K3
  - SpacemiT A100: VLEN 256, DLEN 128
  - SpacemiT X100: VLEN 1024, DLEN 1024 (to be confirmed)
- Cache:
  - L1d: 1 MiB total (16 × 64 KiB)
  - L1i: 1 MiB total (16 × 64 KiB)
  - L2: 10 MiB total (4 × 2560 KiB)
