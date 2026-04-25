# CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
project(WarehouseSolver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -O3 + flags útiles
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(warehouse_solver
    solverHC/main.cpp
    solverHC/geometry.cpp
    solverHC/warehouse_io.cpp
    solverHC/scorer.cpp
    solverHC/validator.cpp
    solverHC/operators.cpp
    solverHC/solver.cpp
)

target_include_directories(warehouse_solver PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/solverHC)

# threads para std::async
find_package(Threads REQUIRED)
target_link_libraries(warehouse_solver PRIVATE Threads::Threads)