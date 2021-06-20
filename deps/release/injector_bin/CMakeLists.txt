cmake_minimum_required (VERSION 3.8)

project ("injector")

include(FetchContent)

set(injector_DOWNLOAD_VERSION 1.0.5)

FetchContent_Declare(
	injector_zip
	URL https://github.com/advancedfx/injector/releases/download/v${injector_DOWNLOAD_VERSION}/HLAE.Injector-${injector_DOWNLOAD_VERSION}-win32.zip
	URL_HASH SHA512=3d77ac4de29872b404607bfd20d2e38eeeb7d411669ecb68601e26a6165b4739041d8a57e5a04828516b978f1fbad8484414e34da36656f9129c60843ae76bf8
	DOWNLOAD_NO_EXTRACT TRUE
)

FetchContent_Declare(
	injector_pdb
	URL https://github.com/advancedfx/injector/releases/download/v${injector_DOWNLOAD_VERSION}/HLAE.Injector-${injector_DOWNLOAD_VERSION}-win32.pdb.zip
	URL_HASH SHA512=73d0520fe8c49182da5f3a9b3d4704de04185444a09f108dcacfb299df73b16c5cfb0ff3ed18cc9c3c33a7f284b03ff39bc8538fcff480282222b5f5bfc5bbdd
	DOWNLOAD_NO_EXTRACT TRUE
)

FetchContent_GetProperties(injector_zip)
if(NOT injector_zip_POPULATED)
  FetchContent_Populate(injector_zip)
  add_custom_target(injector_zip)
endif()

FetchContent_GetProperties(injector_pdb)
if(NOT injector_pdb_POPULATED)
  FetchContent_Populate(injector_pdb)
  add_custom_target(injector_pdb)
endif()

add_custom_target(injector
	DEPENDS injector_zip
	OUTPUT
		"${injector_BINARY_DIR}/AfxHook.dat"
		"${injector_BINARY_DIR}/injector.exe"
		"${injector_BINARY_DIR}/x64/AfxHook.dat"
		"${injector_BINARY_DIR}/x64/injector.exe"
	COMMAND ${CMAKE_COMMAND} -E make_directory bin
	COMMAND ${CMAKE_COMMAND} -E chdir ${injector_BINARY_DIR} ${CMAKE_COMMAND} -E tar xvf  ${injector_zip_SOURCE_DIR}/HLAE.Injector-${injector_DOWNLOAD_VERSION}-win32.zip --format=zip
	COMMAND ${CMAKE_COMMAND} -E chdir ${injector_BINARY_DIR} ${CMAKE_COMMAND} -E tar xvf  ${injector_pdb_SOURCE_DIR}/HLAE.Injector-${injector_DOWNLOAD_VERSION}-win32.pdb.zip --format=zip
)
