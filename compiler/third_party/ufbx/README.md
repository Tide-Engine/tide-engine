# ufbx

Vendored from [ufbx v0.23.1](https://github.com/ufbx/ufbx/tree/26a482ae66871d7de36eb722aa060bce95bce274),
commit `26a482ae66871d7de36eb722aa060bce95bce274`.

`ufbx.h` and `LICENSE` are unchanged upstream files.
`ufbx_math.c` is upstream's `extra/ufbx_math.c` with the fixes below; its Sun Microsystems permission
notice is preserved in the file. ufbx is used under its MIT license, preserved
in `LICENSE`.

Only the compiler and language server link this library. Games receive Tide's
existing packed geometry and model data. FBX parsing uses memory supplied by
the caller, with filesystem access disabled. Builds use the portable math
implementation, strict IEEE arithmetic, and disabled FMA contraction, so the
host platform's transcendental functions cannot change imported results.

Update all four upstream files together and rerun the importer, malformed-file,
native, web, and cross-platform import hash tests when changing this pin.

Local patch: the generic `ufbxi_copy_16_bytes` fallback in `ufbx.c` uses
`memmove` instead of `memcpy`. Inflate can copy a short match whose distance is
less than 16 while writing a padded 16-byte block, so these spans can overlap.
The SSE implementation already loads the complete block before writing.
Linux AddressSanitizer detected the overlap when importing the real compressed
Maya fixtures with `UFBX_STANDARD_C`; the importer regression covers it.

Local portable-math fixes: signed exponent shifts that can have negative
operands use bounded multiplication; sign-bit and low-word operations use
unsigned shifts/subtraction. These preserve the fdlibm bit operations without
undefined signed shifts or negation of the minimum integer. Linux UBSan caught
the negative exponent shift in software square root while loading a Maya rig.
`ufbx_isnan` negates its low word unsigned, as `ufbx_atan2` already did: a
double whose low word is 0x80000000 negated the minimum `int`. Fuzzing
`fbx_file_to_glb` with UBSan found it through scene finalization.
