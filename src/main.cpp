#include "utility/glm_utils/glm_utils.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include "input/glfw_lambda_callback_manager/glfw_lambda_callback_manager.hpp"
#include "input/input_state/input_state.hpp"

#include "utility/texture_packer_model_loading/texture_packer_model_loading.hpp"
#include "utility/fixed_frequency_loop/fixed_frequency_loop.hpp"
#include "utility/unique_id_generator/unique_id_generator.hpp"
#include "utility/resource_path/resource_path.hpp"

#include "sound/sound_types/sound_types.hpp"

#include "system_logic/toolbox_engine/toolbox_engine.hpp"

#include "graphics/input_graphics_sound_menu/input_graphics_sound_menu.hpp"
#include "graphics/rigged_model_loading/rigged_model_loading.hpp"
#include "graphics/vertex_geometry/vertex_geometry.hpp"
#include "graphics/shader_standard/shader_standard.hpp"
#include "graphics/texture_packer/texture_packer.hpp"
#include "graphics/batcher/generated/batcher.hpp"
#include "graphics/shader_cache/shader_cache.hpp"
#include "graphics/fps_camera/fps_camera.hpp"
#include "graphics/draw_info/draw_info.hpp"
#include "graphics/window/window.hpp"
#include "graphics/colors/colors.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char *argv[]) {

    std::vector<ShaderType> requested_shaders = {
        ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES,
        ShaderType::ABSOLUTE_POSITION_WITH_COLORED_VERTEX};
    std::unordered_map<SoundType, std::string> sound_type_to_file = {{SoundType::CLICK, "assets/sounds/click.wav"},
                                                                     {SoundType::HOVER, "assets/sounds/hover.wav"},
                                                                     {SoundType::SUCCESS, "assets/sounds/success.wav"}};

    ToolboxEngine tbx_engine{"mwe_3d_animation", requested_shaders, sound_type_to_file};

    ResourcePath rp(false);

    const auto textures_directory = std::filesystem::path("assets");
    std::filesystem::path output_dir = std::filesystem::path("assets") / "packed_textures";
    int container_side_length = 1024;

    TexturePacker texture_packer(textures_directory, output_dir, container_side_length);
    tbx_engine.shader_cache.set_uniform(
        ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES,
        ShaderUniformVariable::PACKED_TEXTURE_BOUNDING_BOXES, 1);

    glm::mat4 identity = glm::mat4(1);

    std::string path = (argc > 1) ? argv[1] : "assets/animations/shotgun_with_hands.fbx";
    // std::string path = (argc > 1) ? argv[1] : "assets/animations/test.fbx";

    rigged_model_loading::RecIvpntRiggedCollector rirc(
        tbx_engine.batcher
            .texture_packer_rigged_and_animated_cwl_v_transformation_ubos_1024_with_textures_shader_batcher
            .object_id_generator);
    auto ivpntrs = rirc.parse_model_into_ivpntrs(rp.gfp(path).string());
    auto ivpntprs = texture_packer_model_loading::convert_ivpntr_to_ivpntpr(ivpntrs, texture_packer);

    double current_animation_time = 0;
    bool animation_is_playing = false;

    std::string requested_animation = "equip";

    std::function<void(double)> tick = [&](double dt) {
        if (tbx_engine.input_state.is_just_pressed(EKey::p)) {
            animation_is_playing = not animation_is_playing;
        }

        tbx_engine.shader_cache.set_uniform(
            ShaderType::ABSOLUTE_POSITION_WITH_COLORED_VERTEX, ShaderUniformVariable::ASPECT_RATIO,
            glm_utils::tuple_to_vec2(tbx_engine.window.get_aspect_ratio_in_simplest_terms()));

        tbx_engine.shader_cache.set_uniform(
            ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES,
            ShaderUniformVariable::CAMERA_TO_CLIP, tbx_engine.fps_camera.get_projection_matrix());

        tbx_engine.shader_cache.set_uniform(
            ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES,
            ShaderUniformVariable::WORLD_TO_CAMERA, tbx_engine.fps_camera.get_view_matrix());

        // animation start

        tbx_engine.update_active_mouse_mode(tbx_engine.igs_menu_active);
        tbx_engine.update_camera_position_with_default_movement(dt);
        tbx_engine.process_and_queue_render_input_graphics_sound_menu();
        tbx_engine.draw_chosen_engine_stats();

        bool restart_requested = false;

        if (tbx_engine.input_state.is_just_pressed(EKey::q)) {
            requested_animation = "equip";
            restart_requested = true;
        }

        if (tbx_engine.input_state.is_just_pressed(EKey::LEFT_MOUSE_BUTTON)) {
            requested_animation = "fire";
            restart_requested = true;
        }

        // first we upload the animation matrix
        std::vector<glm::mat4> bone_transformations;
        rirc.set_bone_transforms(dt, bone_transformations, requested_animation, false, restart_requested, true);

        const unsigned int MAX_BONES_TO_BE_USED = 100;
        ShaderProgramInfo shader_info = tbx_engine.shader_cache.get_shader_program(
            ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES);

        GLint location = glGetUniformLocation(
            shader_info.id,
            tbx_engine.shader_cache.get_uniform_name(ShaderUniformVariable::BONE_ANIMATION_TRANSFORMS).c_str());

        tbx_engine.shader_cache.use_shader_program(
            ShaderType::TEXTURE_PACKER_RIGGED_AND_ANIMATED_CWL_V_TRANSFORMATION_UBOS_1024_WITH_TEXTURES);
        glUniformMatrix4fv(location, bone_transformations.size(), GL_FALSE, glm::value_ptr(bone_transformations[0]));

        // now the model geometry:
        for (auto &ivpntpr : ivpntprs) {
            // Populate bone_indices and bone_weights
            std::vector<glm::ivec4> bone_indices;
            std::vector<glm::vec4> bone_weights;

            for (const auto &vertex_bone_data : ivpntpr.bone_data) {
                glm::ivec4 indices(static_cast<int>(vertex_bone_data.indices_of_bones_that_affect_this_vertex[0]),
                                   static_cast<int>(vertex_bone_data.indices_of_bones_that_affect_this_vertex[1]),
                                   static_cast<int>(vertex_bone_data.indices_of_bones_that_affect_this_vertex[2]),
                                   static_cast<int>(vertex_bone_data.indices_of_bones_that_affect_this_vertex[3]));

                glm::vec4 weights(vertex_bone_data.weight_value_of_this_vertex_wrt_bone[0],
                                  vertex_bone_data.weight_value_of_this_vertex_wrt_bone[1],
                                  vertex_bone_data.weight_value_of_this_vertex_wrt_bone[2],
                                  vertex_bone_data.weight_value_of_this_vertex_wrt_bone[3]);

                bone_indices.push_back(indices);
                bone_weights.push_back(weights);
            }

            std::vector<int> packed_texture_indices(ivpntpr.xyz_positions.size(), ivpntpr.packed_texture_index);
            int ptbbi = texture_packer.get_packed_texture_bounding_box_index_of_texture(ivpntpr.texture);
            std::vector<int> packed_texture_bounding_box_indices(ivpntpr.xyz_positions.size(), ptbbi);

            // bad!
            std::vector<unsigned int> ltw_indices(ivpntpr.xyz_positions.size(), ivpntpr.id);

            tbx_engine.batcher
                .texture_packer_rigged_and_animated_cwl_v_transformation_ubos_1024_with_textures_shader_batcher
                .queue_draw(ivpntpr.id, ivpntpr.indices, ltw_indices, bone_indices, bone_weights,
                            packed_texture_indices, ivpntpr.packed_texture_coordinates,
                            packed_texture_bounding_box_indices, ivpntpr.xyz_positions);
        }

        tbx_engine.batcher
            .texture_packer_rigged_and_animated_cwl_v_transformation_ubos_1024_with_textures_shader_batcher
            .upload_ltw_matrices();

        tbx_engine.batcher
            .texture_packer_rigged_and_animated_cwl_v_transformation_ubos_1024_with_textures_shader_batcher
            .draw_everything();

        tbx_engine.batcher.absolute_position_with_colored_vertex_shader_batcher.draw_everything();

        tbx_engine.sound_system.play_all_sounds();

        // animation end
    };

    std::function<bool()> termination = [&]() { return tbx_engine.window_should_close(); };

    tbx_engine.start(tick, termination);

    return 0;
}
