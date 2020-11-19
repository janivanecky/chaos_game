#define CPPLIB_PLATFORM_IMPL
#define CPPLIB_GRAPHICS_IMPL
#define CPPLIB_FILESYSTEM_IMPL
#define CPPLIB_MATHS_IMPL
#define CPPLIB_MEMORY_IMPL
#define CPPLIB_UIDRAW_IMPL
#define CPPLIB_UI_IMPL
#define CPPLIB_FONT_IMPL
#define CPPLIB_INPUT_IMPL
#define CPPLIB_COLORS_IMPL
#include "platform.h"
#include "graphics.h"
#include "file_system.h"
#include "maths.h"
#include "memory.h"
#include "ui_draw.h"
#include "ui.h"
#include "font.h"
#include "input.h"
#include "colors.h"
#include <cassert>

int main(int argc, char **argv) {
    // Set up window
    uint32_t window_width = 2560, window_height = 1440;
    uint32_t render_target_width = window_width * 0.5f, render_target_height = window_height * 0.5f;
    HWND window = platform::get_window("Chaos Game", window_width, window_height);
    assert(platform::is_window_valid(window));

    // Init graphics
    graphics::init();
    graphics::init_swap_chain(window, window_width, window_height);

    // Init UI.
    font::init();
    ui::init();
    ui_draw::init((float)window_width, (float)window_height);
    ui::set_background_opacity(0.0f);
    ui::set_input_responsive(true);

    // Create window render target
	RenderTarget render_target_window = graphics::get_render_target_window(false);
    assert(graphics::is_ready(&render_target_window));
    graphics::set_render_targets_viewport(&render_target_window);

    // Vertex shader for displaying textures.
    File vertex_shader_file = file_system::read_file("vertex_shader.hlsl"); 
    VertexShader vertex_shader = graphics::get_vertex_shader_from_code((char *)vertex_shader_file.data, vertex_shader_file.size);
    file_system::release_file(vertex_shader_file);
    assert(graphics::is_ready(&vertex_shader));

    // Pixel shader for displaying textures.
    File pixel_shader_file = file_system::read_file("pixel_shader.hlsl"); 
    PixelShader pixel_shader = graphics::get_pixel_shader_from_code((char *)pixel_shader_file.data, pixel_shader_file.size);
    file_system::release_file(pixel_shader_file);
    assert(graphics::is_ready(&pixel_shader));

    // Chaos game compute shader
    File compute_shader_file = file_system::read_file("compute_shader.hlsl"); 
    ComputeShader compute_shader = graphics::get_compute_shader_from_code((char *)compute_shader_file.data, compute_shader_file.size);
    file_system::release_file(compute_shader_file);
    assert(graphics::is_ready(&compute_shader));

    // Normalization shader
    File normalize_shader_file = file_system::read_file("normalize_shader.hlsl"); 
    ComputeShader normalize_shader = graphics::get_compute_shader_from_code((char *)normalize_shader_file.data, normalize_shader_file.size);
    file_system::release_file(normalize_shader_file);
    assert(graphics::is_ready(&normalize_shader));

    // Simple texture sampler.
    TextureSampler tex_sampler = graphics::get_texture_sampler();
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
        float step;              // Step size
        int iteration;           // Iteration number for progressive rendering
        
        int vertex_count;        // Number of polygon vertices
        float polygon_radius;    // Size of polygon
        int vertex_select_period;// Period with which new vertex is chosen
        int vertex_usage_memory;   // Size of memory of chosen vertices. Prevents last N vertices being chosen.

        int vertex_usage_memory_offset; // Add neighboring vertex (offset by this value) to used vertices instead of the chosen one.
        int filler1;
        int filler2;
        int filler3;
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
    };
    ConstantBuffer config_buffer = graphics::get_constant_buffer(sizeof(Config));

    // Render loop
    bool is_running = true;
    bool show_ui = true;

    Timer timer = timer::get();
    timer::start(&timer);
    FILETIME stored_file_time;
    while(is_running) {
        // Compute FPS.
        float dt = timer::checkpoint(&timer);
        int fps = int(1.0f / dt);

        // Update iteration of progressive rendering.
        config.iteration += 1;

        // Event loop
        {
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
        }

        // React to inputs
        if (!ui::is_registering_input()) {
            // Handle key presses.
            if (input::key_pressed(KeyCode::ESC)) is_running = false; 
            if (input::key_pressed(KeyCode::F1)) show_ui = !show_ui; 

            // Handle mouse wheel scrolling.
            float scroll_delta = input::mouse_scroll_delta();
            if(math::abs(scroll_delta) > 0.0f) {
                config.polygon_radius += input::mouse_scroll_delta() * 10.0f;
                
                // Reset texture and iteration number.
                graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                config.iteration = 0;
            }
        }

         // Shader hot reloading.
        {
            // Get the latest shader file write time.
            char *reload_shader_file = "../compute_shader.hlsl";
            FILETIME current_file_time = file_system::get_last_write_time(reload_shader_file);

            // Check if we've seen this shader before. If not, attempt reload.
            if (CompareFileTime(&current_file_time, &stored_file_time) != 0) {
                // Try to compile the new shader.
                File compute_shader_file = file_system::read_file(reload_shader_file);
                ComputeShader new_compute_shader = graphics::get_compute_shader_from_code(
                    (char *)compute_shader_file.data, compute_shader_file.size)
                ;
                file_system::release_file(compute_shader_file);
                
                // If the compilation was successful, release the old shader and replace with the new one.
                bool reload_success = graphics::is_ready(&new_compute_shader);
                if(reload_success) {
                    graphics::release(&compute_shader);
                    compute_shader = new_compute_shader;

                    // Reset texture and iteration number.
                    graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                    config.iteration = 0;
                }

                // Remember the current shader's write time.
                stored_file_time = current_file_time;
            }
        }

        // Update chaos game texture.
        graphics::set_compute_shader(&compute_shader);
        graphics::set_texture_compute(&compute_texture, 0);
        graphics::set_constant_buffer(&config_buffer, 0);
        graphics::update_constant_buffer(&config_buffer, &config);
        graphics::run_compute(1, 1, 1);

        // Create texture to display by normalizing values in chaos game texture to 0-1 range.
        graphics::set_compute_shader(&normalize_shader);
        graphics::set_texture_compute(&compute_texture, 0);
        graphics::set_texture_compute(&render_texture, 1);
        graphics::run_compute(1, 1, 1);
        graphics::unset_texture_compute(0);
        graphics::unset_texture_compute(1);

        // Draw texture with ray-traced image.
        graphics::set_render_targets_viewport(&render_target_window);
        graphics::clear_render_target(&render_target_window, 0.0f, 0.0f, 0.0f, 1);
        graphics::set_vertex_shader(&vertex_shader);
        graphics::set_pixel_shader(&pixel_shader);
        graphics::set_texture_sampler(&tex_sampler, 0);
        graphics::set_texture(&render_texture, 0);
        graphics::set_texture(&color_palette_texture, 1);
        graphics::draw_mesh(&quad_mesh);
        graphics::unset_texture(0);

        // UI rendering.
        if(show_ui) {
            // Set color of text and UI panel background based on ambient lighting.
            Vector4 text_color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

            // Render FPS and rendering steps counter.
            char text_buffer[100];
            sprintf_s(text_buffer, 100, "FPS %d", fps);
            ui_draw::draw_text(text_buffer, Vector2(10, float(window_height) - 10), text_color, Vector2(0, 1));

            // Render controls UI.
            Panel panel = ui::start_panel("", Vector2(10, 10.0f), 440.0f);
            bool changed = ui::add_slider(&panel, "step size", &config.step, 0.0f, 1.0f);
            changed |= ui::add_slider(&panel, "vertex count", &config.vertex_count, 2, 10);
            changed |= ui::add_slider(&panel, "polygon_radius", &config.polygon_radius, 200.0f, render_target_width / 2.0f);
            // We cannot remember more vertices than we use.
            if(config.vertex_count <= config.vertex_usage_memory) {
                config.vertex_usage_memory = config.vertex_count - 1;
            }
            changed |= ui::add_slider(&panel, "vertex select period", &config.vertex_select_period, 1, 20);
            changed |= ui::add_slider(&panel, "vertex usage memory", &config.vertex_usage_memory, 0, config.vertex_count - 1);
            changed |= ui::add_slider(&panel, "vertex usage memory offset", &config.vertex_usage_memory_offset, 0, config.vertex_count - 1);

            ui::end_panel(&panel);

            // Reset texture and iteration number if any parameter changed.
            if(changed) {
                graphics::clear_texture(&compute_texture, uint32_t(0), 0, 0, 0);
                config.iteration = 0;
            }
        }

        ui::end();
        graphics::swap_frames();
    }

    ui::release();
    graphics::release();

    return 0;
}