cmake_minimum_required (VERSION 3.8)

project ("hlae")

set(hlae_VERSION_MAJOR 2)
set(hlae_VERSION_MINOR 118)
set(hlae_VERSION_PATCH 0)
set(hlae_VERSION_REVISON 0)

add_custom_target(hlae
    DEPENDS afxcppcli afxhookgoldsrc afxhooksource injector
    WORKING_DIRECTORY ${hlae_BINARY_DIR}
    BYPRODUCTS "${hlae_BINARY_DIR}/HLAE.exe"
    COMMAND ${VS16_MSBUILD} "${hlae_SOURCE_DIR}/hlae.csproj" -r "-property:Configuration=Release" "-property:Platform=x86" "/p:OutDir=${hlae_BINARY_DIR}/" "-property:Version=${hlae_VERSION_MAJOR}.${hlae_VERSION_MINOR}.${hlae_VERSION_PATCH}.${hlae_VERSION_REVISON}"
)
