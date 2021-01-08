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
    float normalize_exp;
    float2 offset;

    uint4 points;

    int selection_type;
    float min_t_bezier;
    int step_count;
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

float2 trilinear_to_cartesian(float x, float y, float z, float2 A, float2 B, float2 C) {
    float a = max(length(B - C), 0.00001f);
    float b = max(length(A - C), 0.00001f);
    float c = max(length(A - B), 0.00001f);
    float denominator = a * x + b * y + c * z;
    float2 bA = A * a * x / denominator;
    float2 bB = B * b * y / denominator;
    float2 bC = C * c * z / denominator;
    return bA + bB + bC;
}

static const int max_vertex_count = 10;

int sample_vertex(int seed, inout int used_vertices[max_vertex_count]) {
    // Get number of available vertices.
    int available_vertex_count = vertex_count;
    for(int j = 0; j < vertex_count; ++j) {
        available_vertex_count -= sign(used_vertices[j]);
    }

    // Select a next vertex to go to.
    int selected_vertex = random(seed) * available_vertex_count;
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

    return selected_vertex;
}

[numthreads(1000, 1, 1)]
void main(uint3 threadIDInGroup : SV_GroupThreadID, uint3 group_id : SV_GroupID,
          uint3 dispatchThreadId : SV_DispatchThreadID, uint index : SV_GroupIndex){
    // Particle index.
    uint idx = index + 1000 * group_id.x + 10000 * iteration + 1;

    // Set up polygon vertices.
    float2 vertices[max_vertex_count];
    float2 screen_center = float2(screen_width / 2.0f, screen_height / 2.0f);
    for(int i = 0; i < vertex_count; ++i) {
        float a = i * (PI2 / vertex_count);
        vertices[i].x = sin(a);
        vertices[i].y = -cos(a);
    }

    // Set up initial particle position
    float2 p = float2(
        random(idx * 37) * 2.0f - 1.0f,
        random(idx * 13) * 2.0f - 1.0f
    );

    // Set up array storing used vertices.
    int used_vertices[max_vertex_count];
    for (int i = 0; i < max_vertex_count; ++i) {
        used_vertices[i] = 0;
    }

    // Chaos game loop.
    int selected_vertex = 0;
    [fastopt]
    for(int i = 0; i < 1000; ++i) {
        // Select vertex.
        selected_vertex = sample_vertex(idx * 13 * (i + 1), used_vertices);

        // A, B, C are vertices of a triangle we're going to move within.
        // A is current position and B, C are randomly seleceted neighboring
        // polygon vertices.
        float2 A = p;
        float2 B = vertices[selected_vertex];
        float2 C = vertices[(selected_vertex + 1) % vertex_count];

        // Compute triangle sides.
        float a = max(length(B - C), 0.00001f);
        float b = max(length(A - C), 0.00001f);
        float c = max(length(A - B), 0.00001f);
        
        // Compute incenter point.
        float2 incenter = (a * A + b * B + c * C) / (a + b + c);

        // Compute brocard points.
        float x = c / b;
        float y = a / c;
        float z = b / a;
        float2 brocard1 = trilinear_to_cartesian(x, y, z, A, B, C);

        x = 1.0f / x;
        y = 1.0f / y;
        z = 1.0f / z;
        float2 brocard2 = trilinear_to_cartesian(x, y, z, A, B, C);
        

        // Compute napoleon points
        float angle_A = acos(dot(normalize(B - A), normalize(C - A)));
        float angle_B = acos(dot(normalize(A - B), normalize(C - B)));
        float angle_C = acos(dot(normalize(A - C), normalize(B - C)));

        float2 napoleon1 = trilinear_to_cartesian(
            1.0f/max(sin(angle_A + PI / 6), 0.000001f),
            1.0/max(sin(angle_B + PI / 6), 0.000001f),
            1.0f/max(sin(angle_C + PI / 6), 0.000001f),
            A, B, C
        );

        float2 napoleon2 = trilinear_to_cartesian(
            1.0f/max(sin(angle_A - PI / 6), 0.000001f),
            1.0/max(sin(angle_B - PI / 6), 0.000001f),
            1.0f/max(sin(angle_C - PI / 6), 0.000001f),
            A, B, C
        );

        // Compute isodynamic points.
        float2 isodynamic1 = trilinear_to_cartesian(
            1.0f/max(sin(angle_A + PI / 3), 0.000001f),
            1.0/max(sin(angle_B + PI / 3), 0.000001f),
            1.0f/max(sin(angle_C + PI / 3), 0.000001f),
            A, B, C
        );

        float2 isodynamic2 = trilinear_to_cartesian(
            1.0f/max(sin(angle_A - PI / 3), 0.000001f),
            1.0/max(sin(angle_B - PI / 3), 0.000001f),
            1.0f/max(sin(angle_C - PI / 3), 0.000001f),
            A, B, C
        );
        
        float2 target = vertices[selected_vertex];
        if(selection_type == CONSTANT) {
            // Constant
            if(points[0] & p_vertex0) target = vertices[selected_vertex];
            if(points[0] & p_incenter) target = incenter;
            if(points[0] & p_brocard1) target = brocard1;
            if(points[0] & p_brocard2) target = brocard2;
            if(points[0] & p_napoleon1) target = napoleon1;
            if(points[0] & p_napoleon2) target = napoleon2;
            if(points[0] & p_isodynamic1) target = isodynamic1;
            if(points[0] & p_isodynamic2) target = isodynamic2;
        } else if (selection_type == RANDOM) {
            // Random
            int point_count = 0;
            float2 points_used[8];
            if(points[0] & p_vertex0) points_used[point_count++] = vertices[selected_vertex];
            if(points[0] & p_incenter) points_used[point_count++] = incenter;
            if(points[0] & p_brocard1) points_used[point_count++] = brocard1;
            if(points[0] & p_brocard2) points_used[point_count++] = brocard2;
            if(points[0] & p_napoleon1) points_used[point_count++] = napoleon1;
            if(points[0] & p_napoleon2) points_used[point_count++] = napoleon2;
            if(points[0] & p_isodynamic1) points_used[point_count++] = isodynamic1;
            if(points[0] & p_isodynamic2) points_used[point_count++] = isodynamic2;

            // Select one point at random from used points.
            if(point_count > 0) {
                int point_idx = wang_hash(idx * 33) % point_count;
                target = points_used[point_idx];
            }
        } else if (selection_type == BEZIER_QUADRATIC) {
            // Quadratic Bezier
            float2 bezier_points[3];
            for(int j = 0; j < 3; ++j) {
                if(points[j] & p_current_point) bezier_points[j] = p;
                if(points[j] & p_vertex0) bezier_points[j] = vertices[selected_vertex];
                if(points[j] & p_vertex1) bezier_points[j] = vertices[(selected_vertex + 1) % vertex_count];
                if(points[j] & p_vertex2) bezier_points[j] = vertices[(selected_vertex + 2) % vertex_count];
                if(points[j] & p_incenter) bezier_points[j] = incenter;
                if(points[j] & p_brocard1) bezier_points[j] = brocard1;
                if(points[j] & p_brocard2) bezier_points[j] = brocard2;
                if(points[j] & p_napoleon1) bezier_points[j] = napoleon1;
                if(points[j] & p_napoleon2) bezier_points[j] = napoleon2;
                if(points[j] & p_isodynamic1) bezier_points[j] = isodynamic1;
                if(points[j] & p_isodynamic2) bezier_points[j] = isodynamic2;
            }
            // Compute target point using quadratic bezier equation.
            float t = max(random(idx), min_t_bezier);
            float it = 1 - t;
            target = it * it * bezier_points[0] + 2 * it * t * bezier_points[1] + t * t * bezier_points[2];
        } else if (selection_type == BEZIER_CUBIC) {
            // Cubic Bezier
            float2 bezier_points[4];
            for(int j = 0; j < 4; ++j) {
                if(points[j] & p_current_point) bezier_points[j] = p;
                if(points[j] & p_vertex0) bezier_points[j] = vertices[selected_vertex];
                if(points[j] & p_vertex1) bezier_points[j] = vertices[(selected_vertex + 1) % vertex_count];
                if(points[j] & p_vertex2) bezier_points[j] = vertices[(selected_vertex + 2) % vertex_count];
                if(points[j] & p_incenter) bezier_points[j] = incenter;
                if(points[j] & p_brocard1) bezier_points[j] = brocard1;
                if(points[j] & p_brocard2) bezier_points[j] = brocard2;
                if(points[j] & p_napoleon1) bezier_points[j] = napoleon1;
                if(points[j] & p_napoleon2) bezier_points[j] = napoleon2;
                if(points[j] & p_isodynamic1) bezier_points[j] = isodynamic1;
                if(points[j] & p_isodynamic2) bezier_points[j] = isodynamic2;
            }
            // Compute target point using cubic bezier equation.
            float t = max(random(idx), min_t_bezier);
            float it = 1 - t;
            target = it * it * it * bezier_points[0] +
                3 * it * it * t * bezier_points[1] +
                3 * it * t * t * bezier_points[2] + 
                t * t * t * bezier_points[3];
        }

        // Make `step_count` steps towards the target point.
        float2 step = (target - p) * step_size;
        for(int s = 0; s < step_count; ++s) {
            p += step / float(step_count);
            
            // Project the point from "polygon space" to screen space.
            float2 pp = p;
            pp += offset;
            pp *= polygon_radius;
            pp += screen_center;
            InterlockedAdd(tex[int2(round(pp))], 1);
        }
    }
}