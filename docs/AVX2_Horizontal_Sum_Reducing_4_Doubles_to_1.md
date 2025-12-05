# AVX2 Horizontal Sum: Reducing 4 Doubles to 1

## The Problem

After processing volumes in a loop, you have a 256-bit register containing 4 accumulated sums:

```
sum_vec (__m256d):
┌──────────┬──────────┬──────────┬──────────┐
│  sum0    │  sum1    │  sum2    │  sum3    │  (4 doubles, 256 bits)
└──────────┴──────────┴──────────┴──────────┘
   64 bits    64 bits    64 bits    64 bits

Example values: [100.5, 250.3, 175.8, 300.1]
```

**Goal:** Add all 4 values together to get a single scalar result: `100.5 + 250.3 + 175.8 + 300.1 = 826.7`

---

## The Solution: Horizontal Sum (Reduction)

### Step-by-Step Breakdown

```cpp
// Horizontal sum
__m128d sum_high = _mm256_extractf128_pd(sum_vec, 1);  // Extract upper 128 bits
__m128d sum_low = _mm256_castpd256_pd128(sum_vec);     // Get lower 128 bits
__m128d sum_128 = _mm_add_pd(sum_high, sum_low);       // Add high + low
sum_128 = _mm_hadd_pd(sum_128, sum_128);               // Horizontal add
result = _mm_cvtsd_f64(sum_128);                       // Extract to scalar
```

---

## Visual Step-by-Step

### Initial State:

```
sum_vec (__m256d) = 256 bits
┌─────────────────────────────────────────────────────────┐
│         High 128 bits         │        Low 128 bits     │
├───────────────┬───────────────┼──────────┬──────────────┤
│    sum3       │    sum2       │  sum1    │    sum0      │
│   300.1       │   175.8       │  250.3   │   100.5      │
└───────────────┴───────────────┴──────────┴──────────────┘
 bits 255-192   bits 191-128     bits 127-64  bits 63-0
```

---

### Step 1: `_mm256_extractf128_pd(sum_vec, 1)`

**What it does:** Extracts the **upper 128 bits** (high half) of the 256-bit register

```
Input: sum_vec (256 bits)
┌───────────────┬───────────────┬──────────┬──────────────┐
│    300.1      │    175.8      │  250.3   │    100.5     │
└───────────────┴───────────────┴──────────┴──────────────┘
 └──────── High 128 ────────┘    └───── Low 128 ─────┘
                ↓
         Extract this part (index 1 = high half)

Output: sum_high (__m128d) = 128 bits
┌───────────────┬───────────────┐
│    300.1      │    175.8      │
└───────────────┴───────────────┘
    64 bits        64 bits
```

**Intrinsic signature:**
```cpp
__m128d _mm256_extractf128_pd(__m256d a, const int imm8)
// imm8 = 0: extract low 128 bits
// imm8 = 1: extract high 128 bits
```

---

### Step 2: `_mm256_castpd256_pd128(sum_vec)`

**What it does:** Gets the **lower 128 bits** (low half) of the 256-bit register (zero-cost cast)

```
Input: sum_vec (256 bits)
┌───────────────┬───────────────┬──────────┬──────────────┐
│    300.1      │    175.8      │  250.3   │    100.5     │
└───────────────┴───────────────┴──────────┴──────────────┘
 └──────── High 128 ────────┘    └───── Low 128 ─────┘
                                            ↓
                                    Get this part (cast)

Output: sum_low (__m128d) = 128 bits
┌──────────┬──────────────┐
│  250.3   │    100.5     │
└──────────┴──────────────┘
  64 bits      64 bits
```

**Intrinsic signature:**
```cpp
__m128d _mm256_castpd256_pd128(__m256d a)
// Zero-cost operation (just reinterprets the lower 128 bits)
// No actual data movement!
```

**Note:** This is a "cast" not an "extract" - it's essentially free (0 cycles), just tells the compiler to treat the lower half as a 128-bit register.

---

### Step 3: `_mm_add_pd(sum_high, sum_low)`

**What it does:** Adds corresponding elements from two 128-bit registers (2 doubles each)

```
sum_high:
┌───────────────┬───────────────┐
│    300.1      │    175.8      │
└───────────────┴───────────────┘

sum_low:
┌──────────┬──────────────┐
│  250.3   │    100.5     │
└──────────┴──────────────┘

       ↓ _mm_add_pd (element-wise addition)

sum_128:
┌──────────────┬──────────────┐
│  550.4       │   276.3      │  (300.1+250.3=550.4, 175.8+100.5=276.3)
└──────────────┴──────────────┘
```

**Intrinsic signature:**
```cpp
__m128d _mm_add_pd(__m128d a, __m128d b)
// result[0] = a[0] + b[0]
// result[1] = a[1] + b[1]
```

**Progress:** We've reduced 4 values to 2 values!

---

### Step 4: `_mm_hadd_pd(sum_128, sum_128)`

**What it does:** Horizontal add - adds adjacent pairs within the register

```
sum_128:
┌──────────────┬──────────────┐
│  550.4       │   276.3      │
└──────────────┴──────────────┘
       ↓           ↓
       └───────┬───┘
               ↓
         550.4 + 276.3 = 826.7

Output: sum_128 (overwritten)
┌──────────────┬──────────────┐
│   826.7      │   826.7      │  (both slots contain the same sum!)
└──────────────┴──────────────┘
```

**Intrinsic signature:**
```cpp
__m128d _mm_hadd_pd(__m128d a, __m128d b)
// result[0] = a[0] + a[1]  (horizontal add of a)
// result[1] = b[0] + b[1]  (horizontal add of b)

// When a == b (same register):
// result[0] = a[0] + a[1] = 550.4 + 276.3 = 826.7
// result[1] = a[0] + a[1] = 550.4 + 276.3 = 826.7  (duplicate)
```

**Progress:** We've reduced 2 values to 1 value (duplicated in both slots)!

---

### Step 5: `_mm_cvtsd_f64(sum_128)`

**What it does:** Extracts the **lowest double** from the 128-bit register to a scalar `double`

```
sum_128:
┌──────────────┬──────────────┐
│   826.7      │   826.7      │
└──────────────┴──────────────┘
       ↑
       └─ Extract this (lowest 64 bits)

Output: result (scalar double)
┌──────────────┐
│   826.7      │
└──────────────┘
```

**Intrinsic signature:**
```cpp
double _mm_cvtsd_f64(__m128d a)
// Returns a[0] as a scalar double
```

**Final result:** `826.7` (scalar value)

---

## Complete Visual Flow

```
Step 0: Initial 256-bit register
┌───────────┬───────────┬───────────┬───────────┐
│   300.1   │   175.8   │   250.3   │   100.5   │  sum_vec (__m256d)
└───────────┴───────────┴───────────┴───────────┘
     ↓              ↓           ↓          ↓
     │              │           │          │
     └──────┬───────┘           └────┬─────┘
            ↓                        ↓
Step 1+2: Split into high and low 128-bit halves
     ┌───────────┬───────────┐  ┌───────────┬───────────┐
     │   300.1   │   175.8   │  │   250.3   │   100.5   │
     └───────────┴───────────┘  └───────────┴───────────┘
       sum_high (__m128d)         sum_low (__m128d)
            ↓                          ↓
            └──────────┬───────────────┘
                       ↓
Step 3: Add corresponding elements
            ┌───────────┬───────────┐
            │   550.4   │   276.3   │  sum_128 (__m128d)
            └───────────┴───────────┘
                   ↓          ↓
                   └────┬─────┘
                        ↓
Step 4: Horizontal add (add adjacent elements)
            ┌───────────┬───────────┐
            │   826.7   │   826.7   │  sum_128 (__m128d)
            └───────────┴───────────┘
                   ↓
Step 5: Extract to scalar
                ┌───────────┐
                │   826.7   │  result (double)
                └───────────┘
```

---

## Why This Approach?

### Alternative (Naive) Approach:
```cpp
// Extract each element individually (SLOW!)
double v0 = sum_vec[0];  // Not directly supported!
double v1 = sum_vec[1];  // Would need multiple extracts
double v2 = sum_vec[2];
double v3 = sum_vec[3];
result = v0 + v1 + v2 + v3;
```

### AVX2 Approach (Fast):
```cpp
// Use SIMD instructions to reduce in parallel
__m128d sum_high = _mm256_extractf128_pd(sum_vec, 1);  // 1 instruction
__m128d sum_low = _mm256_castpd256_pd128(sum_vec);     // 0 instructions (cast)
__m128d sum_128 = _mm_add_pd(sum_high, sum_low);       // 1 instruction (2 adds in parallel)
sum_128 = _mm_hadd_pd(sum_128, sum_128);               // 1 instruction
result = _mm_cvtsd_f64(sum_128);                       // 1 instruction

Total: ~4 instructions to sum 4 values!
```

---

## Instruction Details

### `_mm256_extractf128_pd`
- **Operation:** Extract 128 bits from 256-bit register
- **Latency:** ~3 cycles
- **Throughput:** 1 per cycle
- **Use case:** Split wide register into narrower parts

### `_mm256_castpd256_pd128`
- **Operation:** Reinterpret lower 128 bits as __m128d
- **Latency:** 0 cycles (compile-time only)
- **Throughput:** Free
- **Use case:** Access lower half without data movement

### `_mm_add_pd`
- **Operation:** Add two pairs of doubles in parallel
- **Latency:** ~4 cycles
- **Throughput:** 2 per cycle
- **Use case:** Element-wise addition of 128-bit vectors

### `_mm_hadd_pd`
- **Operation:** Horizontal add (sum adjacent elements)
- **Latency:** ~5 cycles
- **Throughput:** 1 per cycle
- **Use case:** Reduce vector to scalar

### `_mm_cvtsd_f64`
- **Operation:** Extract lowest double to scalar
- **Latency:** ~1 cycle
- **Throughput:** 1 per cycle
- **Use case:** Convert SIMD register to regular double

---

## Performance Comparison

### Scalar Reduction (No SIMD):
```cpp
double result = 0.0;
for (size_t i = 0; i < count; ++i) {
    result += volumes[i];
}
// Time: ~1 cycle per addition × count
```

### AVX2 Reduction (Current):
```cpp
// Accumulate 4 at a time in loop
__m256d sum_vec = _mm256_setzero_pd();
for (size_t i = 0; i + 4 <= count; i += 4) {
    __m256d v = _mm256_loadu_pd(&volumes[i]);
    sum_vec = _mm256_add_pd(sum_vec, v);  // 4 additions in 1 instruction
}
// Then horizontal sum (5 instructions)
// Time: ~1 cycle per 4 additions + 5 cycles for reduction
```

**Speedup:** ~3-4x faster

---

## Summary

| Step | Instruction | Input | Output | Purpose |
|------|-------------|-------|--------|---------|
| 1 | `_mm256_extractf128_pd(vec, 1)` | 256-bit (4 doubles) | 128-bit (2 doubles) | Get upper half |
| 2 | `_mm256_castpd256_pd128(vec)` | 256-bit (4 doubles) | 128-bit (2 doubles) | Get lower half (free) |
| 3 | `_mm_add_pd(high, low)` | Two 128-bit | 128-bit (2 doubles) | Add pairs: 4→2 |
| 4 | `_mm_hadd_pd(sum, sum)` | 128-bit (2 doubles) | 128-bit (1 double×2) | Add adjacent: 2→1 |
| 5 | `_mm_cvtsd_f64(sum)` | 128-bit | scalar double | Extract to scalar |

**Result:** Efficiently reduces 4 accumulated doubles to a single scalar sum using SIMD instructions!
