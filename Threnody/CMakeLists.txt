add_executable(Threnody WIN32 "src/Threnody.hpp" "src/Threnody.cpp")

target_compile_features(Threnody PRIVATE cxx_std_20)
target_compile_options(Threnody PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

find_package(kissfft CONFIG REQUIRED)

target_link_libraries(Threnody PRIVATE
    kissfft::kissfft-float
    d2d1 dwrite windowscodecs dcomp
    windowsapp
    mmdevapi
    user32 ole32 shcore
)