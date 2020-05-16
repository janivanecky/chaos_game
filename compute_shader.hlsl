static const float PI = 3.141592f;
static const float PI2 = PI * 2.0f;

RWTexture2D<uint> tex: register(u0);

cbuffer ConfigBuffer : register(b0) {
    int screen_width;
    int screen_height;
    float step_size;
    int iteration;

    int vertex_count;
    float polygon_radius;
    int vertex_select_period;
    int vertex_usage_memory;

    int vertex_usage_memory_offset;
}

uint wang_hash(uint seed) {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return seed;
}

float random(int seed) {
    return float(wang_hash(seed) % 1000000) / 1000000.0f;
}

[numthreads(10, 10, 10)]
void main(uint3 threadIDInGroup : SV_GroupThreadID, uint3 group_id : SV_GroupID,
          uint3 dispatchThreadId : SV_DispatchThreadID, uint index : SV_GroupIndex){
    // Particle index.
    uint idx = index + 1000 * group_id.x;

    // Set up polygon vertices.
    static const int max_vertex_count = 10;
    float2 vertices[max_vertex_count];
    float2 screen_center = float2(screen_width / 2.0f, screen_height / 2.0f);
    for(int i = 0; i < vertex_count; ++i) {
        float a = i * (PI2 / vertex_count);
        vertices[i].x = sin(a) * polygon_radius + screen_center.x;
        vertices[i].y = cos(a) * polygon_radius + screen_center.y;
    }

    // Set up initial particle position
    float2 p = float2(
        random(idx * 11 + iteration * 37) * screen_width,
        random(idx * 13 + iteration * 33) * screen_height
    );

    // Set up array storing used vertices.
    int used_vertices[max_vertex_count];
    for (int i = 0; i < max_vertex_count; ++i) {
        used_vertices[i] = 0;
    }

    // Chaos game loop.
    int selected_vertex = 0;
    for(int i = 0; i < 10000; ++i) {
        // `ii` is total iteration number (across frames).
        int ii = i + 10000 * iteration;
        
        // Select new vertex only every `vertex_select_period` iterations.
        if(i % vertex_select_period == 0) {
            // Get number of available vertices.
            int available_vertex_count = vertex_count;
            for(int j = 0; j < vertex_count; ++j) {
                available_vertex_count -= sign(used_vertices[j]);
            }

            // Select a next vertex to go to.
            selected_vertex = random(idx * 1000 + ii * 311) * available_vertex_count;
            for(int j = 0; j < vertex_count; ++j) {
                if(used_vertices[j] == 0 && j == selected_vertex) {
                    break;
                }
                if(used_vertices[j] > 0) {
                    selected_vertex += 1;
                }
            }

            // Update used vertices' memory.
            for(int j = 0; j < vertex_count; ++j) {
                used_vertices[j] -= sign(used_vertices[j]);
            }

            // Remember currently used vertex (or its neighbor).
            used_vertices[(selected_vertex + vertex_usage_memory_offset) % vertex_count] += vertex_usage_memory;
        }
        // Make a step.
        p += (vertices[selected_vertex] - p) * step_size;
        
        // Register particle position.
        tex[int2(round(p))] += 1;
    }
}