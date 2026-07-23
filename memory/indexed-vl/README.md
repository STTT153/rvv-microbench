# Indexed load for different VL

## Goal

Measure the steady-state cost of an eight-way-unrolled `vluxei32.v` sequence
while varying both vector length (VL) and the index access pattern.

The program detects VLMAX for the selected LMUL at runtime and tests the
complete range `1..VLMAX`. Assuming VLEN=256 and SEW=32, the mapping is:

| LMUL | VLMAX | Tested VL range |
| ---: | ---: | ---: |
| 1 | 8 | 1–8 |
| 2 | 16 | 1–16 |
| 4 | 32 | 1–32 |
| 8 | 64 | 1–64 |

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
2. measures `baseline_kernel` and `indexed_load_kernel` as a pair, using either
   a per-thread `perf_event_open` CPU-cycle counter (`--cycel`, the default) or
   `clock_gettime(CLOCK_MONOTONIC)` (`--wallclock`);
3. repeats the pair for the configured repeat count, alternating which kernel
   runs first;
4. computes `(indexed - baseline) / (iterations * 8)` to obtain cycles or
   nanoseconds per `vluxei32.v`, then divides that value by VL to obtain the
   value per loaded element; and
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

Use `--cycel` for the internal perf-event CPU-cycle counter or `--wallclock`
for `clock_gettime` nanoseconds. They are mutually exclusive; `--cycel` is the
default. The correctly spelled `--cycle` is also accepted as an alias.

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

When using an outer `perf stat`, select `--wallclock` so the program does not
open an internal hardware event. `--kernel baseline` and `--kernel target`
run the two sides separately; `--kernel paired` remains the default and emits
only their normalized difference. For example:

```sh
perf stat -e L1-dcache-loads:u,L1-dcache-load-misses:u \
  taskset -c 3 ./indexed_load --wallclock --kernel baseline \
  --lmul 2 --vl 4 --pattern contiguous --repeats 1

perf stat -e L1-dcache-loads:u,L1-dcache-load-misses:u \
  taskset -c 3 ./indexed_load --wallclock --kernel target \
  --lmul 2 --vl 4 --pattern contiguous --repeats 1
```

Subtract the first external perf count from the second to obtain the target
kernel's baseline-adjusted L1 event count. Use identical arguments for both
commands.

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
Timing = perf_event cycles
Kernel = paired
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

The results below use the current eight-way-unrolled, perf-event-based
implementation.

| Parameter | Value |
| --- | ---: |
| CPU | 3 |

All measurements are paired, baseline-subtracted CPU-cycle values. The
`cycles/vluxei` columns are normalized by `iterations * 8`; the
`cycles/element` columns are normalized once more by VL.

### LMUL = 1

| VL | contiguous cycles/vluxei | contiguous cycles/element | stride 16B cycles/vluxei | stride 16B cycles/element | cacheline 64B cycles/vluxei | cacheline 64B cycles/element | random in page cycles/vluxei | random in page cycles/element |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 107.6752 | 107.6752 | 107.3168 | 107.3168 | 107.2858 | 107.2858 | 107.2480 | 107.2480 |
| 2 | 94.2365 | 47.1183 | 94.1829 | 47.0914 | 94.2945 | 47.1472 | 94.2087 | 47.1043 |
| 3 | 81.1762 | 27.0587 | 81.1800 | 27.0600 | 81.1464 | 27.0488 | 81.1513 | 27.0504 |
| 4 | 68.1056 | 17.0264 | 68.1299 | 17.0325 | 67.9849 | 16.9962 | 67.9777 | 16.9944 |
| 5 | 55.0568 | 11.0114 | 54.9562 | 10.9912 | 54.9433 | 10.9887 | 54.9507 | 10.9901 |
| 6 | 41.9521 | 6.9920 | 41.9643 | 6.9940 | 41.9261 | 6.9877 | 41.9899 | 6.9983 |
| 7 | 28.9220 | 4.1317 | 28.9316 | 4.1331 | 28.9214 | 4.1316 | 28.9267 | 4.1324 |
| 8 | 15.8923 | 1.9865 | 15.9118 | 1.9890 | 15.9093 | 1.9887 | 15.9011 | 1.9876 |

### LMUL = 2

| VL | contiguous cycles/vluxei | contiguous cycles/element | stride 16B cycles/vluxei | stride 16B cycles/element | cacheline 64B cycles/vluxei | cacheline 64B cycles/element | random in page cycles/vluxei | random in page cycles/element |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 219.6995 | 219.6995 | 219.6622 | 219.6622 | 219.8101 | 219.8101 | 219.6577 | 219.6577 |
| 2 | 206.5465 | 103.2733 | 206.2645 | 103.1323 | 206.1956 | 103.0978 | 206.2210 | 103.1105 |
| 3 | 193.1747 | 64.3916 | 193.1711 | 64.3904 | 193.3204 | 64.4401 | 193.2355 | 64.4118 |
| 4 | 180.1732 | 45.0433 | 180.2164 | 45.0541 | 180.2027 | 45.0507 | 180.1310 | 45.0327 |
| 5 | 167.2193 | 33.4439 | 167.1642 | 33.4328 | 167.1389 | 33.4278 | 167.1631 | 33.4326 |
| 6 | 154.1887 | 25.6981 | 154.1056 | 25.6843 | 154.1522 | 25.6920 | 154.1049 | 25.6841 |
| 7 | 141.0796 | 20.1542 | 141.0872 | 20.1553 | 141.0777 | 20.1540 | 141.1579 | 20.1654 |
| 8 | 128.1050 | 16.0131 | 128.0617 | 16.0077 | 128.1181 | 16.0148 | 128.0397 | 16.0050 |
| 9 | 115.0571 | 12.7841 | 115.0594 | 12.7844 | 115.0402 | 12.7822 | 115.0371 | 12.7819 |
| 10 | 102.1296 | 10.2130 | 102.1206 | 10.2121 | 102.0924 | 10.2092 | 102.0424 | 10.2042 |
| 11 | 89.0142 | 8.0922 | 88.9929 | 8.0903 | 89.0096 | 8.0918 | 89.0270 | 8.0934 |
| 12 | 75.9842 | 6.3320 | 75.9777 | 6.3315 | 75.9934 | 6.3328 | 75.9904 | 6.3325 |
| 13 | 63.0047 | 4.8465 | 62.9843 | 4.8449 | 63.0052 | 4.8466 | 62.9614 | 4.8432 |
| 14 | 49.9615 | 3.5687 | 49.9532 | 3.5681 | 49.9415 | 3.5672 | 49.9468 | 3.5676 |
| 15 | 36.9546 | 2.4636 | 37.0533 | 2.4702 | 36.9597 | 2.4640 | 36.9356 | 2.4624 |
| 16 | 23.9106 | 1.4944 | 23.9039 | 1.4940 | 23.9516 | 1.4970 | 23.8946 | 1.4934 |

### LMUL = 4

| VL | contiguous cycles/vluxei | contiguous cycles/element | stride 16B cycles/vluxei | stride 16B cycles/element | cacheline 64B cycles/vluxei | cacheline 64B cycles/element | random in page cycles/vluxei | random in page cycles/element |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 444.3659 | 444.3659 | 444.5230 | 444.5230 | 444.4687 | 444.4687 | 443.5531 | 443.5531 |
| 2 | 430.6563 | 215.3282 | 430.5233 | 215.2617 | 430.6638 | 215.3319 | 430.6791 | 215.3395 |
| 3 | 417.6004 | 139.2001 | 417.6249 | 139.2083 | 417.5584 | 139.1861 | 417.5264 | 139.1755 |
| 4 | 404.5631 | 101.1408 | 404.4771 | 101.1193 | 404.4937 | 101.1234 | 404.5299 | 101.1325 |
| 5 | 391.4551 | 78.2910 | 391.5906 | 78.3181 | 391.5158 | 78.3032 | 391.5098 | 78.3020 |
| 6 | 378.5900 | 63.0983 | 378.4801 | 63.0800 | 378.5164 | 63.0861 | 378.5757 | 63.0959 |
| 7 | 365.4858 | 52.2123 | 365.4601 | 52.2086 | 365.5259 | 52.2180 | 365.5175 | 52.2168 |
| 8 | 352.4509 | 44.0564 | 352.5179 | 44.0647 | 352.4365 | 44.0546 | 352.4049 | 44.0506 |
| 9 | 339.5470 | 37.7274 | 339.4854 | 37.7206 | 339.3786 | 37.7087 | 339.3943 | 37.7105 |
| 10 | 326.5226 | 32.6523 | 326.3726 | 32.6373 | 326.9767 | 32.6977 | 327.0901 | 32.7090 |
| 11 | 313.8716 | 28.5338 | 314.1241 | 28.5567 | 313.6139 | 28.5104 | 313.3613 | 28.4874 |
| 12 | 300.3689 | 25.0307 | 300.4049 | 25.0337 | 300.5936 | 25.0495 | 300.3687 | 25.0307 |
| 13 | 287.3916 | 22.1070 | 287.2668 | 22.0974 | 287.3955 | 22.1073 | 287.3157 | 22.1012 |
| 14 | 274.3058 | 19.5933 | 274.3016 | 19.5930 | 274.4024 | 19.6002 | 274.3409 | 19.5958 |
| 15 | 261.3185 | 17.4212 | 261.2241 | 17.4149 | 261.3187 | 17.4212 | 261.3572 | 17.4238 |
| 16 | 248.3344 | 15.5209 | 248.2643 | 15.5165 | 248.2642 | 15.5165 | 248.3332 | 15.5208 |
| 17 | 235.3881 | 13.8464 | 235.2209 | 13.8365 | 235.2882 | 13.8405 | 235.2336 | 13.8373 |
| 18 | 222.3487 | 12.3527 | 222.2289 | 12.3461 | 222.1992 | 12.3444 | 222.1944 | 12.3441 |
| 19 | 209.2478 | 11.0130 | 209.2705 | 11.0142 | 209.1833 | 11.0096 | 209.1686 | 11.0089 |
| 20 | 196.2460 | 9.8123 | 196.1845 | 9.8092 | 196.2321 | 9.8116 | 196.2104 | 9.8105 |
| 21 | 183.1831 | 8.7230 | 183.1800 | 8.7229 | 183.1861 | 8.7231 | 183.1819 | 8.7229 |
| 22 | 170.2202 | 7.7373 | 170.1842 | 7.7356 | 170.1212 | 7.7328 | 170.0999 | 7.7318 |
| 23 | 157.1185 | 6.8312 | 157.1528 | 6.8327 | 157.1803 | 6.8339 | 157.1074 | 6.8308 |
| 24 | 144.0954 | 6.0040 | 144.0967 | 6.0040 | 144.0688 | 6.0029 | 144.1197 | 6.0050 |
| 25 | 131.1910 | 5.2476 | 131.0617 | 5.2425 | 131.0877 | 5.2435 | 131.1437 | 5.2457 |
| 26 | 118.1221 | 4.5432 | 118.0567 | 4.5406 | 118.0584 | 4.5407 | 118.0266 | 4.5395 |
| 27 | 105.0374 | 3.8903 | 105.0538 | 3.8909 | 105.0051 | 3.8891 | 105.0415 | 3.8904 |
| 28 | 92.0763 | 3.2884 | 92.0058 | 3.2859 | 92.0789 | 3.2885 | 91.9836 | 3.2851 |
| 29 | 79.0057 | 2.7243 | 78.9998 | 2.7241 | 79.0689 | 2.7265 | 78.9925 | 2.7239 |
| 30 | 65.9750 | 2.1992 | 65.9760 | 2.1992 | 65.9773 | 2.1992 | 65.9418 | 2.1981 |
| 31 | 52.9823 | 1.7091 | 52.9408 | 1.7078 | 52.9633 | 1.7085 | 53.0144 | 1.7101 |
| 32 | 39.9338 | 1.2479 | 39.9487 | 1.2484 | 39.9322 | 1.2479 | 39.9677 | 1.2490 |

## Scalar version

The scalar measurements used CPU 3 and the following command:

```sh
taskset -c 3 ./scalar_load --max-vl 32
```

| VL | contiguous cycles/lwu | stride 16B cycles/lwu | cacheline 64B cycles/lwu | random in page cycles/lwu |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0.6250 | 0.6256 | 0.6250 | 0.5000 |
| 2 | 0.2899 | 0.1776 | 0.1341 | 0.2500 |
| 3 | 0.3125 | 0.3980 | 0.3830 | 0.3751 |
| 4 | 0.3576 | 0.3470 | 0.3975 | 0.4062 |
| 5 | 0.4438 | 0.5249 | 0.4682 | 0.4297 |
| 6 | 0.4428 | 0.4588 | 0.4801 | 0.4604 |
| 7 | 0.4560 | 0.5000 | 0.4785 | 0.4861 |
| 8 | 0.4577 | 0.5018 | 0.4765 | 0.4531 |
| 9 | 0.4740 | 0.4877 | 0.4879 | 0.4735 |
| 10 | 0.4783 | 0.4895 | 0.4855 | 0.4987 |
| 11 | 0.4707 | 0.4989 | 0.4906 | 0.4782 |
| 12 | 0.4701 | 0.4805 | 0.5017 | 0.4897 |
| 13 | 0.4823 | 0.5107 | 0.4905 | 0.4719 |
| 14 | 0.4827 | 0.4829 | 0.5053 | 0.4840 |
| 15 | 0.4810 | 0.5010 | 0.4927 | 0.5043 |
| 16 | 0.4878 | 0.5008 | 0.5025 | 0.4966 |
| 17 | 0.4937 | 0.4940 | 0.4941 | 0.5096 |
| 18 | 0.4822 | 0.4963 | 0.5009 | 0.5087 |
| 19 | 0.4869 | 0.5012 | 0.4944 | 0.4946 |
| 20 | 0.4849 | 0.4872 | 0.5007 | 0.5026 |
| 21 | 0.4837 | 0.5072 | 0.4944 | 0.5120 |
| 22 | 0.4911 | 0.4891 | 0.5011 | 0.5064 |
| 23 | 0.4881 | 0.5011 | 0.4956 | 0.5062 |
| 24 | 0.4870 | 0.4999 | 0.5011 | 0.5112 |
| 25 | 0.4941 | 0.4959 | 0.4960 | 0.5158 |
| 26 | 0.4886 | 0.4963 | 0.5014 | 0.5053 |
| 27 | 0.4872 | 0.5005 | 0.4970 | 0.5106 |
| 28 | 0.4919 | 0.4924 | 0.5007 | 0.5143 |
| 29 | 0.4900 | 0.5054 | 0.4965 | 0.5049 |
| 30 | 0.4898 | 0.4920 | 0.5010 | 0.5091 |
| 31 | 0.4912 | 0.5008 | 0.4964 | 0.5125 |
| 32 | 0.4914 | 0.5002 | 0.5010 | 0.5046 |
