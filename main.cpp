#define CPPLIB_JFX_IMPL
#define CPPLIB_PLATFORM_IMPL
#define CPPLIB_GRAPHICS_IMPL
#define CPPLIB_FILESYSTEM_IMPL
#define CPPLIB_MATHS_IMPL
#define CPPLIB_UIDRAW_IMPL
#define CPPLIB_UI_IMPL
#define CPPLIB_FONT_IMPL
#define CPPLIB_INPUT_IMPL
#define CPPLIB_COLORS_IMPL
#include "jfx.h"
#include "platform.h"
#include "graphics.h"
#include "maths.h"
#include "ui_draw.h"
#include "ui.h"
#include "font.h"
#include "input.h"
#include "colors.h"
#include <cassert>

// Some macro hackery so we can expand other macros as strings.
#define XSTRINGIFY(x) #x
#define STRINGIFY(x) XSTRINGIFY(x)

// Selection types definitions.
#define CONSTANT 0
#define RANDOM 1
#define BEZIER_QUADRATIC 2
#define BEZIER_CUBIC 3

char *selection_types[] = {
    XSTRINGIFY(CONSTANT),
    XSTRINGIFY(RANDOM),
    XSTRINGIFY(BEZIER_QUADRATIC),
    XSTRINGIFY(BEZIER_CUBIC),
};

// Point types definitions.
#define current_point   0x1
#define vertex0         0x2
#define vertex1         0x4
#define vertex2         0x8
#define incenter        0x10
#define brocard1        0x20
#define brocard2        0x40
#define napoleon1       0x80
#define napoleon2       0x100
#define isodynamic1     0x200
#define isodynamic2     0x400

// Point types for single-point chaos game.
char *point_types_single[] = {
    XSTRINGIFY(vertex0),
    XSTRINGIFY(incenter),
    XSTRINGIFY(brocard1),
    XSTRINGIFY(brocard2),
    XSTRINGIFY(napoleon1),
    XSTRINGIFY(napoleon2),
    XSTRINGIFY(isodynamic1),
    XSTRINGIFY(isodynamic2)
};

uint32_t point_bits_single[] = {
    vertex0,
    incenter,
    brocard1,
    brocard2,
    napoleon1,
    napoleon2,
    isodynamic1,
    isodynamic2
};

// Point types for bezier curve chaos game.
char *point_types_bezier[] = {
    XSTRINGIFY(current_point),
    XSTRINGIFY(vertex0),
    XSTRINGIFY(vertex1),
    XSTRINGIFY(vertex2),
    XSTRINGIFY(incenter),
    XSTRINGIFY(brocard1),
    XSTRINGIFY(brocard2),
    XSTRINGIFY(napoleon1),
    XSTRINGIFY(napoleon2),
    XSTRINGIFY(isodynamic1),
    XSTRINGIFY(isodynamic2)
};

uint32_t point_bits_bezier[] = {
    current_point,
    vertex0,
    vertex1,
    vertex2,
    incenter,
    brocard1,
    brocard2,
    napoleon1,
    napoleon2,
    isodynamic1,
    isodynamic2
};

// Macros which will be passed to chaos game shader.
char *shader_defines[] = {
    XSTRINGIFY(CONSTANT), STRINGIFY(CONSTANT),
    XSTRINGIFY(RANDOM), STRINGIFY(RANDOM),
    XSTRINGIFY(BEZIER_QUADRATIC), STRINGIFY(BEZIER_QUADRATIC),
    XSTRINGIFY(BEZIER_CUBIC), STRINGIFY(BEZIER_CUBIC),
    "p_" XSTRINGIFY(current_point), STRINGIFY(current_point),
    "p_" XSTRINGIFY(vertex0), STRINGIFY(vertex0),
    "p_" XSTRINGIFY(vertex1), STRINGIFY(vertex1),
    "p_" XSTRINGIFY(vertex2), STRINGIFY(vertex2),
    "p_" XSTRINGIFY(incenter), STRINGIFY(incenter),
    "p_" XSTRINGIFY(brocard1), STRINGIFY(brocard1),
    "p_" XSTRINGIFY(brocard2), STRINGIFY(brocard2),
    "p_" XSTRINGIFY(napoleon1), STRINGIFY(napoleon1),
    "p_" XSTRINGIFY(napoleon2), STRINGIFY(napoleon2),
    "p_" XSTRINGIFY(isodynamic1), STRINGIFY(isodynamic1),
    "p_" XSTRINGIFY(isodynamic2), STRINGIFY(isodynamic2),
};

int main(int argc, char **argv) {
    // Set up window
    uint32_t window_width = 2560, window_height = 1440;
    uint32_t render_target_width = window_width / 2, render_target_height = window_height / 2;
    HWND window = platform::get_window("Chaos Game", window_width, window_height);
    assert(platform::is_window_valid(window));

    // Init graphics
    graphics::init();
    graphics::init_swap_chain(window, window_width, window_height);

    // Init UI.
    font::init();
    ui_draw::init((float)window_width, (float)window_height);
    ui::set_background_opacity(0.0f);
    ui::set_input_responsive(true);

    // Create render targets.
	RenderTarget render_target_window = graphics::get_render_target_window(false);
    assert(graphics::is_ready(&render_target_window));
	RenderTarget render_target_ms = graphics::get_render_target(window_width, window_height, DXGI_FORMAT_R8G8B8A8_UNORM, 8);
    assert(graphics::is_ready(&render_target_ms));

    // Initialize shaders.
    VertexShader vertex_shader = jfx::get_vertex_shader_from_file("vertex_shader.hlsl");
    assert(graphics::is_ready(&vertex_shader));
    PixelShader pixel_shader = jfx::get_pixel_shader_from_file("pixel_shader.hlsl");
    assert(graphics::is_ready(&pixel_shader));
    ComputeShader compute_shader = jfx::get_compute_shader_from_file(
        "compute_shader.hlsl", shader_defines, ARRAYSIZE(shader_defines)
    );
    assert(graphics::is_ready(&compute_shader));
    ComputeShader normalize_shader = jfx::get_compute_shader_from_file("normalize_shader.hlsl");
    assert(graphics::is_ready(&normalize_shader));

    // Simple texture sampler.
    TextureSampler tex_sampler = graphics::get_texture_sampler(SampleMode::BORDER);
    assert(graphics::is_ready(&tex_sampler));

    // Texture where we'll compute particles' position.
    Texture2D compute_texture = graphics::get_texture2D(NULL, render_target_width, render_target_height, DXGI_FORMAT_R32_UINT, 4);
    assert(graphics::is_ready(&compute_texture));
    // Texture which we'll display.
    Texture2D render_texture = graphics::get_texture2D(NULL, render_target_width, render_target_height, DXGI_FORMAT_R32_FLOAT, 4);
    assert(graphics::is_ready(&render_texture));

    // Texture with color palette for rendering.
    Texture2D color_palette_texture = colors::get_palette_magma();
    assert(graphics::is_ready(&color_palette_texture));

    // Quad mesh for rendering the resulting texture.
    Mesh quad_mesh = graphics::get_quad_mesh();

    // Config buffer.
    struct Config {
        int screen_width;       
        int screen_height;
        float step;                     // Step size
        int iteration;                  // Iteration number for progressive rendering
        
        int vertex_count;               // Number of polygon vertices
        float polygon_radius;           // Size of polygon
        int vertex_select_period;       // Period with which new vertex is chosen
        int vertex_usage_memory;        // Size of memory of chosen vertices. Prevents last N vertices being chosen.

        int vertex_usage_memory_offset; // Add neighboring vertex (offset by this value) to used vertices instead of the chosen one.
        float normalize_exponent;       // After density values are normalized to 0-1 range, apply pow(x, 1.0f/normalize_exponent)
        Vector2 offset;                 // Screen space offset.
        
        uint32_t points[4];             // Points selected for chaos game.

        int selection_type;             // Type of point selection.
        float min_t_bezier;             // If bezier point selection is used, this will specify minimum T value for bezier equations.
        int step_count;                 // Number of sub-steps to take when updating particle position.

    };
    Config config = {
        int(render_target_width),
        int(render_target_height),
        0.5f,
        0,
        
        7,
        300.0f,
        1,
        0,

        0,
        1.0f,
        Vector2(0,0),

        {
            vertex0, vertex0, vertex0, vertex0
        },

        CONSTANT,
        0.1f,
        1
    };
    ConstantBuffer config_buffer = graphics::get_constant_buffer(sizeof(Config));

    // Store last shader load times for hot reloading.
    FILETIME compute_shader_file_time;
    FILETIME normalize_shader_file_time;
    FILETIME pixel_shader_file_time;

    // UI state.
    bool selection_combobox_expanded = false;

    // Render loop
    bool is_running = true;
    bool show_ui = true;

    Timer timer = timer::get();
    timer::start(&timer);
    while(is_running) {
        // Compute FPS.
        float dt = timer::checkpoint(&timer);
        int fps = int(1.0f / dt);

        // Event loop
        input::reset();
        Event event;
        while(platform::get_event(&event)) {
            input::register_event(&event);

            // Check if close button pressed
            switch(event.type) {
                case EventType::EXIT: {
                    is_running = false;
                }
                break;
            }
        }

        // Handle key presses.
        if (input::key_pressed(KeyCode::ESC)) is_running = false; 
        if (input::key_pressed(KeyCode::F1)) show_ui = !show_ui; 
        if (input::key_pressed(KeyCode::SPACE)) config.offset = Vector2(0.0f, 0.0f); 

        // React to inputs
        if (!ui::is_registering_input()) {
            // Handle mouse wheel scrolling.
            float scroll_delta = input::mouse_scroll_delta();
            if(math::abs(scroll_delta) > 0.0f) {
                config.polygon_radius += input::mouse_scroll_delta() * 10.0f;
                
                // Reset texture and iteration number.
                graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                config.iteration = 0;
            }

            // Panning.
            if(input::mouse_left_button_down()) {
                Vector2 mouse_delta = Vector2(
                    input::mouse_delta_position_x(),
                    input::mouse_delta_position_y()
                );
                config.offset += mouse_delta / config.polygon_radius / 2.0f;

                // Reset texture and iteration number.
                graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                config.iteration = 0;
            }
        }
        
         // Shader hot reloading.
        jfx::hot_reload_compute_shader(
            &compute_shader, "../compute_shader.hlsl", &compute_shader_file_time, shader_defines, ARRAYSIZE(shader_defines)
        );
        jfx::hot_reload_pixel_shader(&pixel_shader, "../pixel_shader.hlsl", &pixel_shader_file_time);
        jfx::hot_reload_compute_shader(&normalize_shader, "../normalize_shader.hlsl", &normalize_shader_file_time);

        // Update chaos game texture.
        graphics::set_compute_shader(&compute_shader);
        graphics::set_texture_compute(&compute_texture, 0);
        graphics::set_constant_buffer(&config_buffer, 0);
        graphics::update_constant_buffer(&config_buffer, &config);
        graphics::run_compute(10, 1, 1);

        // Create texture to display by normalizing values in chaos game texture to 0-1 range.
        graphics::set_compute_shader(&normalize_shader);
        graphics::set_texture_compute(&compute_texture, 0);
        graphics::set_texture_compute(&render_texture, 1);
        graphics::run_compute(1, 1, 1);
        graphics::unset_texture_compute(0);
        graphics::unset_texture_compute(1);

        // Draw texture.
        graphics::set_render_targets_viewport(&render_target_ms);
        graphics::clear_render_target(&render_target_ms, 0.0f, 0.0f, 0.0f, 1);
        graphics::set_vertex_shader(&vertex_shader);
        graphics::set_pixel_shader(&pixel_shader);
        graphics::set_texture_sampler(&tex_sampler, 0);
        graphics::set_texture(&render_texture, 0);
        graphics::set_texture(&color_palette_texture, 1);
        graphics::draw_mesh(&quad_mesh);
        graphics::unset_texture(0);

        // UI rendering.
        if(show_ui) {
            // Render FPS and rendering steps counter.
            char text_buffer[100];
            sprintf_s(text_buffer, 100, "FPS %d", fps);
            ui_draw::draw_text(
                text_buffer, Vector2(10, float(window_height) - 10), Vector4(1.0f, 1.0f, 1.0f, 1.0f), Vector2(0, 1)
            );

            sprintf_s(text_buffer, 100, "ITERATION %d", config.iteration);
            ui_draw::draw_text(
                text_buffer, Vector2(10, float(window_height) - 10 - font::get_row_height(ui_draw::get_font())), Vector4(1.0f, 1.0f, 1.0f, 1.0f), Vector2(0, 1)
            );

            // Settings UI.
            Panel panel = ui::start_panel("SETTINGS", Vector2(10, 10.0f));
            bool changed = false;

            changed = ui::add_slider(&panel, "step size", &config.step, 0.0f, 1.0f);
            changed |= ui::add_slider(&panel, "vertex count", &config.vertex_count, 2, 10);
            // We cannot remember more vertices than we use.
            if(config.vertex_count <= config.vertex_usage_memory) {
                config.vertex_usage_memory = config.vertex_count - 1;
            }
            changed |= ui::add_slider(&panel, "vertex select period", &config.vertex_select_period, 1, 20);
            changed |= ui::add_slider(&panel, "vertex usage memory", &config.vertex_usage_memory, 0, config.vertex_count - 1);
            changed |= ui::add_slider(&panel, "vertex usage memory offset", &config.vertex_usage_memory_offset, 0, config.vertex_count - 1);
            changed |= ui::add_slider(&panel, "step_count", &config.step_count, 1, 20);
            changed |= ui::add_slider(&panel, "min_t_bezier", &config.min_t_bezier, 0.0f, 1.0f);
            changed |= ui::add_slider(&panel, "normalize exp", &config.normalize_exponent, 0.0f, 10.0f);
            changed |= ui::add_combobox(
                &panel, "selection type", selection_types, ARRAYSIZE(selection_types), &config.selection_type, &selection_combobox_expanded
            );

            ui::end_panel(&panel);

            float panel_width = 225.0f;
            // Handle single point selection (CONSTANT and RANDOM types).
            if(config.selection_type <= RANDOM) {
                panel = ui::start_panel("POINT TYPES", Vector2(window_width - 10 - panel_width, 10));
                
                for(uint32_t i = 0; i < ARRAYSIZE(point_types_single); ++i) {
                   // Create UI toggle element.
                    bool is_selected = config.points[0] & point_bits_single[i];
                    changed |= ui::add_toggle(&panel, point_types_single[i], &is_selected);
                    
                    // Update the point selection based on selection type.
                    if(config.selection_type == CONSTANT && is_selected) {
                        config.points[0] = point_bits_single[i];
                    } else if (config.selection_type == RANDOM) {
                        // Clear the bit for current point.
                        config.points[0] &= ~point_bits_single[i];
                        // Set the bit for current point.
                        config.points[0] |= point_bits_single[i] * is_selected;
                    }
                }

                ui::end_panel(&panel);
            // Handle bezier curve point selection.
            } else {
                Vector2 panel_pos = Vector2(window_width - 10 - panel_width, 10.0f);
                int point_count = config.selection_type == BEZIER_QUADRATIC ? 3 : 4;

                // Set up panels from right to left, so from the last point to the first.
                for(int j = point_count - 1; j >= 0; --j) {
                    // Set up panel name.
                    static char title_buffer[20];
                    sprintf_s(title_buffer, 20, "POINT %d", j);
                    panel = ui::start_panel(title_buffer, panel_pos);

                    for(uint32_t i = 0; i < ARRAYSIZE(point_types_bezier); ++i) {
                        // Create UI toggle element.
                        bool is_selected = config.points[j] & point_bits_bezier[i];
                        changed |= ui::add_toggle(&panel, point_types_bezier[i], &is_selected);
                        
                        // Update point selection.
                        if(is_selected) {
                            config.points[j] = point_bits_bezier[i];
                        }
                    }

                    ui::end_panel(&panel);

                    panel_pos.x -= panel_width - 10.0f;
                }
            }

            // Reset texture and iteration number if any parameter changed.
            if(changed) {
                graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                config.iteration = 0;
            }
        }

        ui::end_frame();


        graphics::set_render_targets_viewport(&render_target_window);
        graphics::resolve_render_targets(&render_target_ms, &render_target_window);
        graphics::swap_frames();

        // Update iteration of progressive rendering.
        config.iteration += 1;
    }

    graphics::release();

    return 0;
}