cmake_minimum_required(VERSION 3.15)

# 静的ライブラリ
project(R2sampler C)
set(LIB_NAME r2sampler)
add_library(${LIB_NAME}
    STATIC
    $<TARGET_OBJECTS:ring_buffer>
    $<TARGET_OBJECTS:r2sampler_rate_converter>
    )

# 依存するプロジェクト
add_subdirectory(libs)

# テスト
if(NOT without-test)
    enable_testing()
    # C++環境でないとgtestがビルドできないので、CXXプロジェクトを作る
    # -> Cとの挙動で差異が生じるかもしれない...
    project(R2samplerTest CXX)
    if(MSVC)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    else()
        set(CMAKE_CXX_FLAGS "-std=gnu++11") # gtestがGNU独自拡張を使用しているため
        set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
    endif()
    include(cmake/gtest.cmake)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_subdirectory(test)
endif()
