add_executable(flash3ds_tests
    TestMain.cpp
    SwfTestFixtures.cpp
    test_swf_reader.cpp
    test_swf_loader.cpp
    test_movie.cpp
    test_swf_records.cpp
    test_place_object_tag.cpp
    test_display_list.cpp
    test_timeline.cpp
)

target_include_directories(flash3ds_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(flash3ds_tests PRIVATE flash3ds_core)

add_test(NAME flash3ds_tests COMMAND flash3ds_tests)
