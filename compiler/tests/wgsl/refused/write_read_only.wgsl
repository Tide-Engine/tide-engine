// refused: that's read-only
@group(0) @binding(33) var<storage, read> given: array<u32>;
@compute @workgroup_size(1) fn compute() {
    given[0] = 1u;
}
