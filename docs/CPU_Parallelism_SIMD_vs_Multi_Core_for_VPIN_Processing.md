# CPU Parallelism: SIMD vs Multi-Core for VPIN Processing

## Your CPU Configuration

```
Intel Xeon @ 2.50GHz
├── Physical Cores: 3
├── Logical CPUs: 6 (Hyper-Threading: 2 threads per core)
└── SIMD Width: 512-bit (AVX-512) or 256-bit (AVX2)

CPU Topology:
┌─────────────────────────────────────┐
│  Physical CPU Socket 0              │
│  ┌───────────┬───────────┬────────┐ │
│  │  Core 0   │  Core 1   │ Core 2 │ │
│  │  ┌─┬─┐    │  ┌─┬─┐    │ ┌─┬─┐  │ │
│  │  │0│1│    │  │2│3│    │ │4│5│  │ │  ← Logical CPUs (threads)
│  │  └─┴─┘    │  └─┴─┘    │ └─┴─┘  │ │
│  └───────────┴───────────┴────────┘ │
└─────────────────────────────────────┘
```

---

## CRITICAL DISTINCTION: SIMD ≠ Multi-Core

### ❌ What AVX2/AVX-512 DOES NOT DO:
**AVX2/AVX-512 does NOT distribute work across multiple CPU cores!**

### ✅ What AVX2/AVX-512 ACTUALLY DOES:
**It processes multiple data elements in parallel WITHIN A SINGLE CORE using wide registers.**

---

## How `compute_imbalances_avx2()` Actually Works

### Current Code (AVX2):
```cpp
void compute_imbalances_avx2(const double* buy_vols, const double* sell_vols,
                             size_t count, double* imbalances) {
    size_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m256d buy = _mm256_loadu_pd(&buy_vols[i]);   // Load 4 doubles
        __m256d sell = _mm256_loadu_pd(&sell_vols[i]); // Load 4 doubles
        __m256d diff = _mm256_sub_pd(buy, sell);       // Subtract 4 pairs
        
        __m256d sign_mask = _mm256_set1_pd(-0.0);
        __m256d abs_diff = _mm256_andnot_pd(sign_mask, diff); // Abs 4 values
        
        _mm256_storeu_pd(&imbalances[i], abs_diff);    // Store 4 results
    }
    // ... handle remainder
}
```

### Execution Model:

```
Single CPU Core (e.g., Core 0, Thread 0)
┌────────────────────────────────────────────────────────────┐
│                                                            │
│  Iteration 1: Process 4 imbalances in ONE instruction     │
│  ┌──────────────────────────────────────────────────┐     │
│  │ buy[0]  │ buy[1]  │ buy[2]  │ buy[3]  │ (256-bit)│     │
│  │ sell[0] │ sell[1] │ sell[2] │ sell[3] │ register │     │
│  │    ↓    │    ↓    │    ↓    │    ↓    │          │     │
│  │  diff0  │  diff1  │  diff2  │  diff3  │          │     │
│  │    ↓    │    ↓    │    ↓    │    ↓    │          │     │
│  │  |d0|   │  |d1|   │  |d2|   │  |d3|   │          │     │
│  └──────────────────────────────────────────────────┘     │
│                                                            │
│  Iteration 2: Process next 4 imbalances                   │
│  ┌──────────────────────────────────────────────────┐     │
│  │ buy[4]  │ buy[5]  │ buy[6]  │ buy[7]  │          │     │
│  │ sell[4] │ sell[5] │ sell[6] │ sell[7] │          │     │
│  │    ↓    │    ↓    │    ↓    │    ↓    │          │     │
│  │  diff4  │  diff5  │  diff6  │  diff7  │          │     │
│  └──────────────────────────────────────────────────┘     │
│                                                            │
└────────────────────────────────────────────────────────────┘

Other cores (1, 2, 3, 4, 5): IDLE (not used by this code!)
```

**Key Point:** This runs on **ONE CPU core only**. The other 5 logical CPUs are doing nothing!

---

## SIMD Parallelism (Data-Level Parallelism)

### What Happens in Hardware:

```
Single Core's Execution Unit:
┌─────────────────────────────────────────────────┐
│  AVX2 Vector ALU (256-bit wide)                 │
│  ┌──────┬──────┬──────┬──────┐                  │
│  │ ALU0 │ ALU1 │ ALU2 │ ALU3 │  ← 4 parallel    │
│  │  64b │  64b │  64b │  64b │     operations   │
│  └──────┴──────┴──────┴──────┘                  │
│     ↓      ↓      ↓      ↓                       │
│   res0   res1   res2   res3                      │
└─────────────────────────────────────────────────┘

All 4 operations happen in ONE CPU cycle!
```

**This is NOT multi-threading** - it's a single thread using wide vector instructions.

---

## Multi-Core Parallelism (Thread-Level Parallelism)

### What You DON'T Have (but could implement):

```
Core 0, Thread 0:  Process buckets 0-24   (imbalances[0:25])
Core 1, Thread 2:  Process buckets 25-49  (imbalances[25:50])
Core 2, Thread 4:  Process buckets 50-74  (imbalances[50:75])
                   ↓
All cores work simultaneously on different data!
```

**This would require:**
- Multi-threading (e.g., OpenMP, std::thread, pthread)
- Explicit work distribution across cores
- Synchronization/coordination

**Your current code does NOT do this!**

---

## How Tick Processing Works in VPIN

### When a Tick Arrives:

```
1. Tick arrives → Kafka → C++ VPIN processor
                    ↓
2. Single thread processes on ONE core:
   ┌─────────────────────────────────────┐
   │ Core 0, Thread 0                    │
   │ ├─ Classify tick (buy/sell)         │
   │ ├─ Add to current bucket            │
   │ ├─ Check if bucket is full          │
   │ └─ If full:                          │
   │    ├─ compute_imbalances_avx2()     │ ← Uses SIMD (4 at once)
   │    ├─ sum_volumes_avx2()            │ ← Uses SIMD (4 at once)
   │    └─ Calculate VPIN                │
   └─────────────────────────────────────┘
   
   Cores 1-5: IDLE (not used!)
```

### Bucketing Process:

```
Tick Stream:  T1 → T2 → T3 → T4 → T5 → ...
               ↓
Volume-Based Bucketing (e.g., 50 buckets, 10,000 volume each)
┌────────┬────────┬────────┬────────┬─────
│ Bucket │ Bucket │ Bucket │ Bucket │ ...
│   0    │   1    │   2    │   3    │
│ 10K vol│ 10K vol│ 10K vol│ 10K vol│
└────────┴────────┴────────┴────────┴─────

When bucket fills:
  ↓
compute_imbalances_avx2(buy_vols[0:50], sell_vols[0:50], 50, imbalances)
  ↓
Process 50 buckets in ~13 iterations (4 buckets per iteration with AVX2)
  ↓
All on ONE core!
```

---

## Performance Breakdown

### For 50 Buckets (Typical VPIN Window):

#### Scalar (No SIMD):
```
Iterations: 50 (one bucket at a time)
Cores Used: 1
Time:       ████████████████████████████████████████████████ (100%)
```

#### AVX2 (Current):
```
Iterations: 13 (4 buckets per iteration, 2 remainder)
Cores Used: 1
Time:       █████████████ (25-30% of scalar)
Speedup:    3-4x faster
```

#### AVX-512 (Possible):
```
Iterations: 7 (8 buckets per iteration, 2 remainder)
Cores Used: 1
Time:       ███████ (12-15% of scalar)
Speedup:    6-8x faster
```

#### Multi-threaded + AVX-512 (Not Implemented):
```
Iterations: 2 per core (split 50 buckets across 3 cores)
Cores Used: 3
Time:       ███ (4-6% of scalar)
Speedup:    15-20x faster (theoretical)
```

---

## How to Check CPU Usage

### While VPIN is Running:

```bash
# Real-time CPU usage per core
htop

# Or:
top -H

# Or detailed per-core stats:
mpstat -P ALL 1
```

You'll see something like:
```
CPU    %usr   %sys  %iowait   %idle
0     95.0    2.0      0.0     3.0   ← VPIN running here
1      0.5    0.2      0.0    99.3   ← Mostly idle
2      0.3    0.1      0.0    99.6   ← Mostly idle
3      0.2    0.1      0.0    99.7   ← Mostly idle
4      0.1    0.0      0.0    99.9   ← Mostly idle
5      0.1    0.0      0.0    99.9   ← Mostly idle
```

**Only ONE core is busy!** The others are idle.

---

## Summary

### ❓ "How does work get distributed among CPUs?"

**Answer:** It doesn't! Your current VPIN code runs on **ONE CPU core only**.

### ❓ "How do I know which core it's using?"

**Answer:** Run `htop` or `top -H` and look for 100% usage on one core.

### ❓ "What does AVX2 do then?"

**Answer:** AVX2 processes **4 data elements in parallel WITHIN that single core** using 256-bit wide registers. It's data-level parallelism, not thread-level parallelism.

### ❓ "When ticks arrive, how is bucketing done?"

**Answer:** 
1. Ticks arrive sequentially (one at a time)
2. Single thread on ONE core processes each tick
3. Adds tick to current bucket
4. When bucket fills → runs `compute_imbalances_avx2()` on that same core
5. AVX2 speeds up the calculation by processing 4 buckets at once
6. But it's still ONE core doing all the work

### 🚀 To Use Multiple Cores:

You'd need to implement multi-threading:
```cpp
#pragma omp parallel for
for (int i = 0; i < num_buckets; i++) {
    imbalances[i] = std::abs(buy_vols[i] - sell_vols[i]);
}
```

This would distribute buckets across cores, AND each core could use AVX2/AVX-512 for its assigned buckets!

---

## Visual Summary

```
┌─────────────────────────────────────────────────────────┐
│                    Your System                          │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Current VPIN Implementation:                           │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 0: ████████████████████ (100% busy)     │      │
│  │         ↑ Single thread + AVX2               │      │
│  │         ↑ Processes 4 buckets per iteration  │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 1: ░░░░░░░░░░░░░░░░░░░░ (idle)          │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 2: ░░░░░░░░░░░░░░░░░░░░ (idle)          │      │
│  └──────────────────────────────────────────────┘      │
│                                                         │
│  Potential with Multi-threading + AVX-512:              │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 0: ████████ (33% busy, 8 buckets/iter)  │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 1: ████████ (33% busy, 8 buckets/iter)  │      │
│  └──────────────────────────────────────────────┘      │
│  ┌──────────────────────────────────────────────┐      │
│  │ Core 2: ████████ (33% busy, 8 buckets/iter)  │      │
│  └──────────────────────────────────────────────┘      │
│                                                         │
│  Speedup: 15-20x faster!                                │
└─────────────────────────────────────────────────────────┘
```
