# Installation-time runtime deployment and CPack packaging.
# SPDX-License-Identifier: AGPL-3.0-only
#
# Windows portable distribution:
#   cmake --build build --config Release
#   cmake --install build --config Release --prefix dist/FurnitureOS   (runs windeployqt)
#   cpack --config build/CPackConfig.cmake -C Release                   (ZIP + SHA256)
# The result runs from any folder without installation or administrator rights.

option(FOS_DEPLOY_QT "Run windeployqt at install time (Windows, GUI builds)" ON)

if(WIN32 AND FOS_BUILD_GUI AND FOS_DEPLOY_QT AND TARGET furnitureos)
    get_target_property(_fos_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_fos_qt_bin "${_fos_qmake}" DIRECTORY)
    find_program(FOS_WINDEPLOYQT windeployqt HINTS "${_fos_qt_bin}" NO_DEFAULT_PATH)
    if(NOT FOS_WINDEPLOYQT)
        find_program(FOS_WINDEPLOYQT windeployqt)
    endif()
    if(FOS_WINDEPLOYQT)
        # --compiler-runtime copies the MSVC runtime DLLs (vcruntime/msvcp) so the folder also
        # runs on machines without the Visual C++ Redistributable. opengl32sw.dll (software
        # OpenGL) is deployed for machines without a usable OpenGL driver.
        install(CODE "
            execute_process(
                COMMAND \"${FOS_WINDEPLOYQT}\" --release --compiler-runtime --no-system-d3d-compiler
                        --no-quick-import --no-translations
                        \"\${CMAKE_INSTALL_PREFIX}/furnitureos.exe\"
                RESULT_VARIABLE _fos_deploy_result)
            if(NOT _fos_deploy_result EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed with \${_fos_deploy_result}\")
            endif()")
    else()
        message(WARNING "windeployqt not found: the installed application will lack the Qt runtime.")
    endif()
endif()

set(CPACK_PACKAGE_NAME "FurnitureOS")
set(CPACK_PACKAGE_VENDOR "FurnitureOS contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Free, open-source furniture CAD/CAM")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
if(WIN32)
    set(CPACK_GENERATOR ZIP)
    set(CPACK_SYSTEM_NAME "windows-x64")
else()
    set(CPACK_GENERATOR TGZ)
    set(CPACK_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "FurnitureOS-${PROJECT_VERSION}-${CPACK_SYSTEM_NAME}")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
include(CPack)
