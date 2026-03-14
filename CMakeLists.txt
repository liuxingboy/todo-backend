cmake_minimum_required(VERSION 3.15)
project(todo_backend CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Trantor REQUIRED)
find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

# 自动下载并配置 jwt-cpp
include(FetchContent)
FetchContent_Declare(
    jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG v0.6.0
)
FetchContent_MakeAvailable(jwt-cpp)

# 自动收集我们新建的目录下的所有源文件
file(GLOB_RECURSE SRC_FILES 
    "main.cc"
    "controllers/*.cc"
    "utils/*.cc"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

# 链接核心库与 jwt-cpp
target_link_libraries(${PROJECT_NAME} PRIVATE 
    Drogon::Drogon 
    jwt-cpp::jwt-cpp 
    OpenSSL::Crypto
)
