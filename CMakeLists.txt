cmake_minimum_required(VERSION 3.20)

project(PdfToImage VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "PdfToImage is a Windows-only application")
endif()

if(MSVC AND CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "Win32")
  message(FATAL_ERROR "Configure with -A Win32; one x86 build supports both x86 and x64 Windows")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0601 WINVER=0x0601
)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8 /EHsc)
  add_link_options(/LARGEADDRESSAWARE)
endif()

set(PDFIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pdfium")
set(PDFIUM_INCLUDE_DIR "${PDFIUM_ROOT}/include")
set(PDFIUM_LIBRARY "${PDFIUM_ROOT}/lib/pdfium.dll.lib")
set(PDFIUM_DLL "${PDFIUM_ROOT}/bin/pdfium.dll")

foreach(required_path IN ITEMS "${PDFIUM_INCLUDE_DIR}/fpdfview.h" "${PDFIUM_LIBRARY}" "${PDFIUM_DLL}")
  if(NOT EXISTS "${required_path}")
    message(FATAL_ERROR "Missing locked PDFium dependency: ${required_path}")
  endif()
endforeach()

add_library(pdf_common STATIC
  src/common/folder_opener.cpp
  src/common/logger.cpp
  src/common/output_path.cpp
  src/common/protocol.cpp
  src/common/utf.cpp
)
target_include_directories(pdf_common PUBLIC src)
target_link_libraries(pdf_common PUBLIC shell32)

add_executable(PdfWorker
  src/worker/worker_main.cpp
  src/worker/image_pdf_builder.cpp
  src/worker/pdf_renderer.cpp
  src/worker/png_encoder.cpp
  resources/worker.rc
)
target_include_directories(PdfWorker PRIVATE src "${PDFIUM_INCLUDE_DIR}")
target_link_libraries(PdfWorker PRIVATE pdf_common "${PDFIUM_LIBRARY}" windowscodecs ole32)
if(MSVC)
  target_link_options(PdfWorker PRIVATE /SUBSYSTEM:CONSOLE,6.01 /MANIFEST:NO)
endif()

add_executable(PdfToImage WIN32
  src/app/main.cpp
  src/app/app_window.cpp
  src/app/app_settings.cpp
  src/app/drop_handler.cpp
  src/app/worker_client.cpp
  resources/app.rc
)
target_include_directories(PdfToImage PRIVATE src)
target_link_libraries(PdfToImage PRIVATE pdf_common d2d1 dwrite comdlg32 shell32 shlwapi ole32)
if(MSVC)
  target_link_options(PdfToImage PRIVATE /SUBSYSTEM:WINDOWS,6.01 /MANIFEST:NO)
endif()

foreach(target IN ITEMS PdfWorker PdfToImage)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFIUM_DLL}" "$<TARGET_FILE_DIR:${target}>/pdfium.dll"
  )
endforeach()

enable_testing()

add_executable(common_tests tests/unit/common_tests.cpp)
target_include_directories(common_tests PRIVATE src)
target_link_libraries(common_tests PRIVATE pdf_common)
add_test(NAME common_tests COMMAND common_tests)

add_executable(renderer_tests
  tests/unit/renderer_tests.cpp
  src/worker/image_pdf_builder.cpp
  src/worker/pdf_renderer.cpp
  src/worker/png_encoder.cpp
)
target_include_directories(renderer_tests PRIVATE src "${PDFIUM_INCLUDE_DIR}")
target_link_libraries(renderer_tests PRIVATE pdf_common "${PDFIUM_LIBRARY}" windowscodecs ole32)
add_custom_command(TARGET renderer_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFIUM_DLL}" "$<TARGET_FILE_DIR:renderer_tests>/pdfium.dll"
)
add_test(NAME renderer_tests COMMAND renderer_tests)

add_executable(worker_process_tests tests/unit/worker_process_tests.cpp)
target_include_directories(worker_process_tests PRIVATE src)
target_link_libraries(worker_process_tests PRIVATE pdf_common)
add_dependencies(worker_process_tests PdfWorker)
add_test(NAME worker_process_tests COMMAND worker_process_tests "$<TARGET_FILE:PdfWorker>")

install(TARGETS PdfToImage PdfWorker RUNTIME DESTINATION .)
install(FILES "${PDFIUM_DLL}" DESTINATION .)
install(FILES "${PDFIUM_ROOT}/LICENSE" DESTINATION . RENAME THIRD_PARTY_NOTICES.txt)
