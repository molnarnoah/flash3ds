add_executable(flash3ds_tests
    TestMain.cpp
    SwfTestFixtures.cpp
    Avm1TestFixtures.cpp
    test_swf_reader.cpp
    test_swf_loader.cpp
    test_movie.cpp
    test_swf_records.cpp
    test_place_object_tag.cpp
    test_display_list.cpp
    test_timeline.cpp
    test_shape_records.cpp
    test_define_shape_tag.cpp
    test_character_dictionary.cpp
    test_shape_tessellator.cpp
    test_software_renderer.cpp
    test_scene_renderer.cpp
    test_avm1_value.cpp
    test_avm1_scope.cpp
    test_avm1_interpreter.cpp
    test_movieclip_instance.cpp
    test_define_sound_tag.cpp
    test_input_state.cpp
    test_define_font_tag.cpp
    test_define_text_tag.cpp
    test_define_button_tag.cpp
    test_define_edit_text_tag.cpp
)

target_include_directories(flash3ds_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(flash3ds_tests PRIVATE flash3ds_core)

add_test(NAME flash3ds_tests COMMAND flash3ds_tests)
