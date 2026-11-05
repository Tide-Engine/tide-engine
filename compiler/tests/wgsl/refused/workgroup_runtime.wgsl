// refused: a workgroup's array has a count
var<workgroup> held: array<u32>;
@compute @workgroup_size(1) fn compute() {
    held[0] = 1u;
}
