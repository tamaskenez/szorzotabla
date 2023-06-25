find_package(fmt REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(szorzotabla
    main.cpp
    utils.cpp
    utils.h
)

target_link_libraries(szorzotabla
    PRIVATE
        fmt::fmt
        nlohmann_json::nlohmann_json
)
