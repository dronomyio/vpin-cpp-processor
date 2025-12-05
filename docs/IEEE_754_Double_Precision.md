# IEEE 754 Double Precision: Why -0.0 = 0x8000000000000000

## IEEE 754 Double-Precision Format (64 bits)

```
┌─────┬──────────────┬────────────────────────────────────────────────────┐
│  S  │   Exponent   │                  Mantissa (Fraction)               │
├─────┼──────────────┼────────────────────────────────────────────────────┤
│ 1   │   11 bits    │                    52 bits                         │
└─────┴──────────────┴────────────────────────────────────────────────────┘
 bit 63  bits 62-52                    bits 51-0

Total: 64 bits = 8 bytes
```

### Bit Layout:
- **Bit 63**: Sign bit (0 = positive, 1 = negative)
- **Bits 62-52**: Exponent (11 bits, biased by 1023)
- **Bits 51-0**: Mantissa/Fraction (52 bits)

---

## Positive Zero: +0.0

```
Decimal: +0.0
Binary representation:

┌─┬───────────┬────────────────────────────────────────────────────┐
│0│00000000000│0000000000000000000000000000000000000000000000000000│
└─┴───────────┴────────────────────────────────────────────────────┘
 ↑      ↑                           ↑
 │      │                           └─ Mantissa = 0
 │      └─ Exponent = 0
 └─ Sign = 0 (positive)

Hexadecimal: 0x0000000000000000
             │││││││││││││││││
             0000 0000 0000 0000
             
Binary:      0000 0000 0000 0000 ... (all 64 bits are 0)
```

---

## Negative Zero: -0.0

```
Decimal: -0.0
Binary representation:

┌─┬───────────┬────────────────────────────────────────────────────┐
│1│00000000000│0000000000000000000000000000000000000000000000000000│
└─┴───────────┴────────────────────────────────────────────────────┘
 ↑      ↑                           ↑
 │      │                           └─ Mantissa = 0
 │      └─ Exponent = 0
 └─ Sign = 1 (negative)  ← ONLY DIFFERENCE!

Hexadecimal: 0x8000000000000000
             │││││││││││││││││
             8000 0000 0000 0000
             
Binary:      1000 0000 0000 0000 ... (only bit 63 is 1)
             ↑
             └─ This is the sign bit!
```

---

## Why 0x8000000000000000?

### Breaking Down the Hex:

```
0x8000000000000000
  │└──────┬──────┘
  │       └─ 15 zeros (60 bits of zeros)
  └─ 8 in hex = 1000 in binary

Bit-by-bit:
Hex:  8    0    0    0    0    0    0    0    0    0    0    0    0    0    0    0
Bin: 1000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000
     ↑    └──────────────────────────────────────────────────────────────────────┘
     │                         All zeros (63 bits)
     └─ Sign bit = 1 (bit 63)
```

### Visual Breakdown:

```
64-bit double: -0.0

Bit positions:
63  62  61  60  59  58  57  56  ...  3   2   1   0
┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
│ 1 │ 0 │ 0 │ 0 │ 0 │ 0 │ 0 │ 0 │...│ 0 │ 0 │ 0 │ 0 │
└───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
  ↑   └────────────────────────────────────────────┘
  │                  All zeros
  └─ Sign bit (most significant bit)

Grouped into bytes:
Byte 7    Byte 6    Byte 5    Byte 4    Byte 3    Byte 2    Byte 1    Byte 0
10000000  00000000  00000000  00000000  00000000  00000000  00000000  00000000
   0x80      0x00      0x00      0x00      0x00      0x00      0x00      0x00

Result: 0x8000000000000000
```

---

## How _mm256_set1_pd(-0.0) Works

### Step 1: Interpret -0.0 as Bit Pattern

```
C++ code:
double value = -0.0;

Memory representation:
┌──────────────────────────┐
│ 0x8000000000000000       │  ← 8 bytes
└──────────────────────────┘
```

### Step 2: Broadcast to 256-bit Register

```
_mm256_set1_pd(-0.0)

Creates 256-bit register with 4 copies:

┌──────────────────────────┬──────────────────────────┬──────────────────────────┬──────────────────────────┐
│ 0x8000000000000000       │ 0x8000000000000000       │ 0x8000000000000000       │ 0x8000000000000000       │
│     (double 0)           │     (double 1)           │     (double 2)           │     (double 3)           │
└──────────────────────────┴──────────────────────────┴──────────────────────────┴──────────────────────────┘
└───────── 64 bits ────────┴───────── 64 bits ────────┴───────── 64 bits ────────┴───────── 64 bits ────────┘
└────────────────────────────────────────── 256 bits total ──────────────────────────────────────────────────┘

Each 64-bit slot contains:
┌─┬───────────┬────────────────────────────────────────────────────┐
│1│00000000000│0000000000000000000000000000000000000000000000000000│
└─┴───────────┴────────────────────────────────────────────────────┘
 ↑
 └─ Sign bit = 1
```

---

## Using as a Mask for Absolute Value

### Original Values (with signs):

```
diff register (4 doubles):
┌──────────────┬──────────────┬──────────────┬──────────────┐
│   -5.3       │    2.7       │   -1.2       │    0.8       │
└──────────────┴──────────────┴──────────────┴──────────────┘

Bit representation:
┌──────────────┬──────────────┬──────────────┬──────────────┐
│1xxxxxxxxxxx..│0xxxxxxxxxxx..│1xxxxxxxxxxx..│0xxxxxxxxxxx..│
└──────────────┴──────────────┴──────────────┴──────────────┘
 ↑             ↑              ↑              ↑
 Sign=1 (neg)  Sign=0 (pos)   Sign=1 (neg)  Sign=0 (pos)
```

### Sign Mask (from -0.0):

```
sign_mask = _mm256_set1_pd(-0.0)
┌──────────────┬──────────────┬──────────────┬──────────────┐
│10000000000...│10000000000...│10000000000...│10000000000...│
└──────────────┴──────────────┴──────────────┴──────────────┘
 ↑             ↑              ↑              ↑
 Only sign bit set in each double
```

### AND-NOT Operation:

```
abs_diff = _mm256_andnot_pd(sign_mask, diff)

Step 1: NOT sign_mask
┌──────────────┬──────────────┬──────────────┬──────────────┐
│01111111111...│01111111111...│01111111111...│01111111111...│
└──────────────┴──────────────┴──────────────┴──────────────┘
 ↑
 Sign bit cleared, all other bits set

Step 2: AND with diff
Result: Clears sign bit, keeps all other bits
┌──────────────┬──────────────┬──────────────┬──────────────┐
│0xxxxxxxxxxx..│0xxxxxxxxxxx..│0xxxxxxxxxxx..│0xxxxxxxxxxx..│
└──────────────┴──────────────┴──────────────┴──────────────┘
 ↑             ↑              ↑              ↑
 All sign bits = 0 (positive)

Final values:
┌──────────────┬──────────────┬──────────────┬──────────────┐
│    5.3       │    2.7       │    1.2       │    0.8       │
└──────────────┴──────────────┴──────────────┴──────────────┘
All positive! (absolute values)
```

---

## Summary

### Why 0x8000000000000000?

1. **IEEE 754 format**: Sign bit is bit 63 (most significant bit)
2. **-0.0 representation**: Sign=1, Exponent=0, Mantissa=0
3. **Binary**: `1000 0000 0000 0000 ... 0000` (only bit 63 is 1)
4. **Hexadecimal**: `0x8000000000000000`

### Why use -0.0 as a mask?

```
-0.0 = 0x8000000000000000 = Only sign bit set

Perfect for isolating or clearing the sign bit!

AND-NOT with this mask → Clears sign bit → Absolute value
```

### Visual Memory Layout:

```
Memory address:  [0x00] [0x01] [0x02] [0x03] [0x04] [0x05] [0x06] [0x07]
                  ────   ────   ────   ────   ────   ────   ────   ────
Value: -0.0       0x80   0x00   0x00   0x00   0x00   0x00   0x00   0x00
                   ↑
                   └─ 0x80 = 0b10000000 (sign bit set)

In 256-bit register:
[double 0] [double 1] [double 2] [double 3]
 0x8000...  0x8000...  0x8000...  0x8000...
```

**Bottom line:** `0x8000000000000000` is the bit pattern where ONLY the sign bit (bit 63) is set to 1, making it perfect for manipulating signs in floating-point operations!
