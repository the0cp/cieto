# Benchmark Results

Last updated: 2026-09-20

## Current Results

| Benchmark | Median (ms) | Range (ms) |
|---|---:|---:|
| arithmetic_loop | 181.631 | 178.385–184.735 |
| equality_hot | 140.458 | 136.112–150.001 |
| function_calls | 141.809 | 130.770–213.571 |
| gc_churn | 13.812 | 12.777–14.102 |
| list_ops | 56.182 | 49.831–58.948 |
| map_ops | 55.487 | 49.930–61.599 |
| object_dispatch | 288.120 | 269.109–311.841 |
| string_ops | 91.120 | 83.449–94.646 |

## Previous Comparison

| Workload | Metric | Previous | Current | Change |
|---|---|---:|---:|---:|
| append | Time (ms) | 2.830 | 1.359 | -51.98% |
| append | Instructions | 184,383,732 | 75,725,025 | -58.93% |
| append | Cycles | 82,606,332 | 28,034,333 | -66.06% |
| append | Branches | 35,544,028 | 10,658,395 | -70.01% |
| strfind | Time (ms) | 2.151 | 0.679 | -68.42% |
| strfind | Instructions | 155,766,683 | 49,609,917 | -68.15% |
| strfind | Cycles | 62,928,513 | 15,313,633 | -75.67% |
| strfind | Branches | 32,148,099 | 7,452,945 | -76.82% |
| numeric | Instructions | 871,207,261 | 734,715,758 | -15.67% |
| numeric | Cycles | 389,859,960 | 339,818,948 | -12.84% |
| numeric | Branches | 110,502,294 | 100,754,380 | -8.82% |
| numeric | Callgrind Ir | 870,998,317 | 734,497,634 | -15.67% |
| numeric | `OP_MOVE` Ir | 114,000,472 | 36,000,504 | -68.42% |
| numeric | Bytecode instructions | 566 | 532 | -6.01% |
| calls | Instructions | 1,003,951,729 | 993,458,039 | -1.05% |
| array | Instructions | 91,127,858 | 88,328,964 | -3.07% |

## Cross-Language Results

All values are median milliseconds.

| Workload | Cieto | C | Go | Java JIT | Java -Xint | Python | Node | Bash |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| numeric, 300k loops | 29.177 | 0.976 | 2.442 | 3.092 | 14.785 | 28.848 | 3.548 | — |
| calls, 150k loops | 19.235 | 0.856 | 1.311 | 0.952 | 13.446 | 25.342 | 0.170 | — |
| 12 × fib(18) | 3.825 | 0.105 | 0.234 | 0.170 | 2.020 | 3.868 | 0.364 | — |
| array update, 20k items | 2.193 | 0.041 | 0.132 | 0.295 | 0.996 | 2.653 | 3.169 | 165.988 |
| append, 20k items | 3.687 | 0.033 | 0.366 | 0.724 | 6.691 | 1.566 | 2.776 | 155.022 |
| map insert/read, 3k items | 0.671 | 0.009 | 0.502 | 0.562 | 2.226 | 0.374 | 0.399 | 17.192 |
| substring find, 20k calls | 2.840 | ≈0* | 0.150 | 1.048 | 5.587 | 0.838 | 0.843 | 87.078 |
| string `+`, 1.5k calls | 1.438 | 0.001 | 0.006 | 0.064 | 0.393 | 0.055 | 0.058 | 2.732 |
| insertion sort, 500 items | 3.197 | 0.030 | 0.066 | 0.208 | 1.313 | 4.013 | 0.150 | — |
| file read, 25 × 256 KiB | 0.751 | 0.271 | 0.850 | 8.971 | 79.851 | 1.652 | 4.583 | 34.088 |
| file write, 20 × 64 KiB | 3.632 | 2.737 | 3.418 | 9.674 | 26.981 | 3.498 | 3.657 | 24.353 |

`*` The C compiler moved the repeated `strstr` call out of the loop.

## Startup Results

| Implementation | Median (ms) | Range (ms) |
|---|---:|---:|
| Cieto | 2.745 | 2.618–2.923 |
| C | 2.788 | 2.480–2.866 |
| Go | 2.062 | 2.043–2.121 |
| Java JIT | 41.025 | 40.335–42.107 |
| Java -Xint | 29.865 | 28.798–31.384 |
| Python | 16.266 | 15.500–16.611 |
| Node | 30.625 | 30.195–31.732 |
| Bash | 3.837 | 3.564–5.114 |
