# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

project ("advancedfx")

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}")

#
# Get Microsoft Visual Studio related paths:
#

execute_process(
    COMMAND "$ENV{ProgramFiles\(x86\)}\\Microsoft Visual Studio\\Installer\\vswhere.exe" "-latest" "-version" "[16.0,17.0)" "-property" "installationPath"
    OUTPUT_VARIABLE VS16_INSTALLPATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\\" "/" VS16_INSTALLPATH ${VS16_INSTALLPATH})

execute_process(
    COMMAND "$ENV{ProgramFiles\(x86\)}\\Microsoft Visual Studio\\Installer\\vswhere.exe" "-latest" "-version" "[16.0,17.0)" "-requires" "Microsoft.Component.MSBuild" "-find" "MSBuild\\**\\**\\Bin\\MSBuild.exe"
    OUTPUT_VARIABLE VS16_MSBUILD
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\\" "/" VS16_MSBUILD ${VS16_MSBUILD})

#
# Include dependencies:
#

include(FetchContent)
include(ExternalProject)

add_subdirectory("deps/release/zlib")
add_subdirectory("deps/release/protobuf")
add_subdirectory("deps/release/openexr")
add_subdirectory("deps/release/injector_bin")

add_subdirectory("AfxCppCli")

ExternalProject_Add("shaderbuilder"
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/ShaderBuilder"
	BINARY_DIR "${CMAKE_BINARY_DIR}/ShaderBuilder"
    CMAKE_GENERATOR "Visual Studio 16 2019"
    CMAKE_GENERATOR_TOOLSET "v142"
    CMAKE_GENERATOR_PLATFORM "x64"
    BUILD_COMMAND "${CMAKE_COMMAND}" --build . --config "${CMAKE_CFG_INTDIR}" -- -r
	INSTALL_COMMAND ""
)

set(shaderbuilder_BINARY_DIR "${CMAKE_BINARY_DIR}/ShaderBuilder/${CMAKE_CFG_INTDIR}")

add_subdirectory("AfxHookGoldSrc")
add_subdirectory("AfxHookSource")
add_subdirectory("hlae")


#
# Update translation files
#

add_custom_target(l10n_update
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND "${CMAKE_SOURCE_DIR}/l10n_update_to_source.bat"
)

#
# advancedfx
#

add_custom_target(advancedfx ALL
	DEPENDS hlae
)

#
# Install
#

set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
include(InstallRequiredSystemLibraries)
set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "${MSVC_CRT_DIR}/vccorlib${v}.dll")


install(
    PROGRAMS
        ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}

        "${afxcppcli_BINARY_DIR}/AfxCppCli.dll"
        "${afxhookgoldsrc_BINARY_DIR}/AfxHookGoldSrc.dll"
        "${afxhooksource_BINARY_DIR}/AfxHookSource.dll"
        "${hlae_BINARY_DIR}/HLAE.exe"
        "${hlae_BINARY_DIR}/NGettext.dll"
        "${injector_BINARY_DIR}/injector.exe"

        "${zlib_SOURCE_DIR}/zlib1.dll"

	    "${ILMBASE_BUILD_DIR}/bin/Half-2_5.dll"
        "${ILMBASE_BUILD_DIR}/bin/Iex-2_5.dll"
        "${ILMBASE_BUILD_DIR}/bin/IexMath-2_5.dll"
        "${ILMBASE_BUILD_DIR}/bin/IlmThread-2_5.dll"
        "${ILMBASE_BUILD_DIR}/bin/Imath-2_5.dll"
        "${OPENEXR_BUILD_DIR}/bin/IlmImf-2_5.dll"

    DESTINATION "bin"
)
install(
    FILES
        "${injector_BINARY_DIR}/AfxHook.dat"
    DESTINATION "bin"
)

install(
    FILES
        "${afxcppcli_BINARY_DIR}/AfxCppCli.pdb"
        "${afxhookgoldsrc_BINARY_DIR}/AfxHookGoldSrc.pdb"
        "${afxhooksource_BINARY_DIR}/AfxHookSource.pdb"
        "${hlae_BINARY_DIR}/HLAE.pdb"
        "${injector_BINARY_DIR}/injector.pdb"

    DESTINATION "pdb"
)

install(
    PROGRAMS
        "${injector_BINARY_DIR}/x64/injector.exe"
    DESTINATION "bin/x64"
)
install(
    FILES
        "${injector_BINARY_DIR}/x64/AfxHook.dat"
    DESTINATION "bin/x64"
)
install(
    FILES
        "${injector_BINARY_DIR}/x64/injector.pdb"
    DESTINATION "pdb/x64"
)

install(
	CODE
	"
	execute_process( WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\" COMMAND \"${CMAKE_SOURCE_DIR}/copy_resources_release.bat\" \"${CMAKE_INSTALL_PREFIX}/bin\" )
	execute_process( WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}\" COMMAND \"${CMAKE_SOURCE_DIR}/l10n_update_to_source.bat\" \"${CMAKE_INSTALL_PREFIX}/bin\" )
	
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/bin\" COMMAND \"${CMAKE_COMMAND}\" -E tar cfv  \"${CMAKE_INSTALL_PREFIX}/hlae.zip\" --format=zip -- . )
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}/pdb\" COMMAND \"${CMAKE_COMMAND}\" -E tar cfv  \"${CMAKE_INSTALL_PREFIX}/hlae_pdb.zip\" --format=zip -- . )

	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E make_directory installer)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E make_directory installer/hlae)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E copy_directory bin installer/hlae)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E rm -Rf installer/hlae/ffmpeg)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E rm -Rf installer/hlae/locales)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E rm -f installer/hlae/HLAE.exe)
	execute_process( WORKING_DIRECTORY \"${CMAKE_SOURCE_DIR}/installer/setup\" COMMAND \"${VS16_MSBUILD}\" \"${CMAKE_SOURCE_DIR}/installer/setup/setup.wixproj\" -property:Configuration=Release -property:Platform=x86 \"-property:OutputPath=${CMAKE_INSTALL_PREFIX}/installer/build\")
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E copy installer/build/HLAE_Setup.exe ./HLAE_Setup.exe)
	execute_process( WORKING_DIRECTORY \"${CMAKE_INSTALL_PREFIX}\" COMMAND \"${CMAKE_COMMAND}\" -E copy installer/build/HLAE_Setup.wixpdb ./HLAE_Setup.wixpdb)
	"
)
