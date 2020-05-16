RWTexture2D<uint> tex_in: register(u0);
RWTexture2D<float> tex_out: register(u1);

cbuffer ConfigBuffer : register(b0) {
    int screen_width;
    int screen_height;
}

// Arrays to store intermediate min/max values.
groupshared uint vals_max[1024];
groupshared uint vals_min[1024];

[numthreads(32, 32, 1)]
void main(uint3 threadIDInGroup : SV_GroupThreadID, uint3 groupID : SV_GroupID,
          uint3 dispatchThreadId : SV_DispatchThreadID, uint gidx : SV_GroupIndex){
    // Image patch size each thread will operate on - read from and write to.
    int2 patch_size = int2(screen_width, screen_height) / 32;

    // Get "patch id".
    int2 idx = dispatchThreadId.xy * patch_size;

    // Get minimum and maximum value for the patch.
    float max_val = 0.0f;
    float min_val = 10000.0f;
    for (int y = 0; y < patch_size.y; y++) {
        for (int x = 0; x < patch_size.x; x++) {
            int2 pos = idx + int2(x, y);
            max_val = max(max_val, tex_in[pos]);
            min_val = min(min_val, tex_in[pos]);
        }
    }

    // Store min/max values into shared memory.
    vals_min[gidx] = min_val;
    vals_max[gidx] = max_val;
    GroupMemoryBarrierWithGroupSync();
    
    // GPU Reduce of min/max values.
    uint max_idx = 512;
    for (; max_idx > 0; max_idx /= 2) {
        if (gidx < max_idx) {
            // Get max value for this reduce cycle.
            uint v1 = vals_max[gidx];
            uint v2 = vals_max[gidx + max_idx];
            uint v_max = max(v1, v2);

            // Get min value for this reduce cycle.
            v1 = vals_min[gidx];
            v2 = vals_min[gidx + max_idx];
            uint v_min = min(v1, v2);

            // Store min/max values.
            vals_max[gidx] = v_max;
            vals_min[gidx] = v_min;
        }
        GroupMemoryBarrierWithGroupSync();
    }
    
    // Normalize values in the assigned patch.
    for (int y = 0; y < patch_size.y; y++) {
        for (int x = 0; x < patch_size.x; x++) {
            int2 pos = idx + int2(x, y);
            tex_out[pos] = float(tex_in[pos] - vals_min[0]) / float(vals_max[0] - vals_min[0]);
        }
    }
}