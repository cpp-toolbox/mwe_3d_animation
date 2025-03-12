#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "graphics/draw_info/draw_info.hpp"
#include "input/glfw_lambda_callback_manager/glfw_lambda_callback_manager.hpp"
#include "input/input_state/input_state.hpp"

#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"

#include "graphics/vertex_geometry/vertex_geometry.hpp"
#include "graphics/shader_standard/shader_standard.hpp"
#include "graphics/batcher/generated/batcher.hpp"
#include "graphics/shader_cache/shader_cache.hpp"
#include "graphics/fps_camera/fps_camera.hpp"
#include "graphics/window/window.hpp"
#include "graphics/colors/colors.hpp"
#include "utility/unique_id_generator/unique_id_generator.hpp"

#include <iostream>

int main() {
    Colors colors;

    FPSCamera fps_camera;

    unsigned int window_width_px = 700, window_height_px = 700;
    bool start_in_fullscreen = false;
    bool start_with_mouse_captured = true;
    bool vsync = false;

    Window window;
    window.initialize_glfw_glad_and_return_window(window_width_px, window_height_px, "catmullrom camera interpolation",
                                                  start_in_fullscreen, start_with_mouse_captured, vsync);

    InputState input_state;

    std::vector<ShaderType> requested_shaders = {ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR};
    ShaderCache shader_cache(requested_shaders);
    Batcher batcher(shader_cache);

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
    shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                             ShaderUniformVariable::RGBA_COLOR, glm::vec4(colors.cyan, 1));

    std::function<void(unsigned int)> char_callback = [](unsigned int codepoint) {};
    std::function<void(int, int, int, int)> key_callback = [&](int key, int scancode, int action, int mods) {
        input_state.glfw_key_callback(key, scancode, action, mods);
    };
    std::function<void(double, double)> mouse_pos_callback = [&](double xpos, double ypos) {
        fps_camera.mouse_callback(xpos, ypos);
    };
    std::function<void(int, int, int)> mouse_button_callback = [&](int button, int action, int mods) {
        input_state.glfw_mouse_button_callback(button, action, mods);
    };
    std::function<void(int, int)> frame_buffer_size_callback = [](int width, int height) {};
    GLFWLambdaCallbackManager glcm(window.glfw_window, char_callback, key_callback, mouse_pos_callback,
                                   mouse_button_callback, frame_buffer_size_callback);

    auto ball = vertex_geometry::generate_icosphere(3, 1);
    Transform ball_transform;

    glm::mat4 identity = glm::mat4(1);
    shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                             ShaderUniformVariable::CAMERA_TO_CLIP,
                             fps_camera.get_projection_matrix(window_width_px, window_height_px));

    GLuint ltw_matrices_gl_name;
    BoundedUniqueIDGenerator ltw_id_generator(1024);
    glm::mat4 ltw_matrices[1024];

    // initialize all matrices to identity matrices
    for (int i = 0; i < 1024; ++i) {
        ltw_matrices[i] = identity;
    }

    glGenBuffers(1, &ltw_matrices_gl_name);
    glBindBuffer(GL_UNIFORM_BUFFER, ltw_matrices_gl_name);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(ltw_matrices), ltw_matrices, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ltw_matrices_gl_name);

    std::function<void(double)> tick = [&](double dt) {
        /*glfwGetFramebufferSize(window, &width, &height);*/

        glViewport(0, 0, window_width_px, window_width_px);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        fps_camera.process_input(input_state.is_pressed(EKey::LEFT_CONTROL), input_state.is_pressed(EKey::TAB),
                                 input_state.is_pressed(EKey::w), input_state.is_pressed(EKey::a),
                                 input_state.is_pressed(EKey::s), input_state.is_pressed(EKey::d),
                                 input_state.is_pressed(EKey::SPACE), input_state.is_pressed(EKey::LEFT_SHIFT), dt);

        shader_cache.set_uniform(ShaderType::CWL_V_TRANSFORMATION_UBOS_1024_WITH_SOLID_COLOR,
                                 ShaderUniformVariable::WORLD_TO_CAMERA, fps_camera.get_view_matrix());

        std::vector<unsigned int> ltw_indices(ball.xyz_positions.size(), 0);
        batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.queue_draw(
            0, ball.indices, ball.xyz_positions, ltw_indices);

        ltw_matrices[0] = ball_transform.get_transform_matrix();

        batcher.cwl_v_transformation_ubos_1024_with_solid_color_shader_batcher.draw_everything();

        glBindBuffer(GL_UNIFORM_BUFFER, ltw_matrices_gl_name);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(ltw_matrices), ltw_matrices);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        glfwSwapBuffers(window.glfw_window);
        glfwPollEvents();

        TemporalBinarySignal::process_all();
    };

    std::function<bool()> termination = [&]() { return glfwWindowShouldClose(window.glfw_window); };

    FixedFrequencyLoop ffl;
    ffl.start(120, tick, termination);

    return 0;
}
