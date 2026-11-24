// SPDX-License-Identifier: GPL-3.0-or-later
// Two planes of 16-byte vectors, with the same total buffer size as four uint2 planes.

__device__ __forceinline__ void load_vectors(const uint2* hash, size_t count, size_t owner,
                                             uint2* result, bool live) {
    const auto* packed = reinterpret_cast<const uint4*>(hash);
    const auto empty = make_uint4(0, 0, 0, 0);
    const auto lo = live ? packed[owner] : empty;
    const auto hi = live ? packed[count + owner] : empty;
    result[0] = make_uint2(lo.x, lo.y);
    result[1] = make_uint2(lo.z, lo.w);
    result[2] = make_uint2(hi.x, hi.y);
    result[3] = make_uint2(hi.z, hi.w);
}

__device__ __forceinline__ void store_vectors(uint2* hash, size_t count, size_t owner, const uint2* value) {
    auto* packed = reinterpret_cast<uint4*>(hash);
    packed[owner] = make_uint4(value[0].x, value[0].y, value[1].x, value[1].y);
    packed[count + owner] = make_uint4(value[2].x, value[2].y, value[3].x, value[3].y);
}
