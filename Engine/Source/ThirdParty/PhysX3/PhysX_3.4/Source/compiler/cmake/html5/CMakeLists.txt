cmake_minimum_required(VERSION 3.3)

MESSAGE("[PhysX]cmake/html5/CMakeList.txt")
#set(CMAKE_VERBOSE_MAKEFILE ON)
include(../common/CMakeLists.txt)


IF(NOT DEFINED TARGET_BUILD_PLATFORM) # Not defined, default to HTML5
	SET(TARGET_BUILD_PLATFORM "html5")
ENDIF()

SET(PLATFORM_LIST html5)

IF (NOT ${TARGET_BUILD_PLATFORM} IN_LIST PLATFORM_LIST)
	MESSAGE(FATAL_ERROR "Invalid platform:" ${TARGET_BUILD_PLATFORM})
ENDIF()

SET(CMAKE_CXX_FLAGS "-fdiagnostics-show-option -fno-rtti -fno-exceptions -ffast-math -ffunction-sections -fdata-sections -Werror -ferror-limit=0 -Wall -Wextra -fstrict-aliasing -Wstrict-aliasing=2 -pedantic -Weverything -Wno-c++11-long-long -Wno-padded -Wno-reserved-id-macro -Wno-float-equal -Wno-sign-conversion -Wno-covered-switch-default -Wno-documentation-unknown-command -Wno-weak-vtables -Wno-missing-prototypes -Wno-unused-local-typedef -Wno-float-conversion -Wno-global-constructors -Wno-missing-variable-declarations -Wno-exit-time-destructors -Wno-unused-macros -Wno-undef -Wno-c++11-extra-semi -Wno-c++11-extensions -Wno-non-virtual-dtor -Wno-unknown-pragmas -Wno-old-style-cast -Wno-extra-semi -Wno-cast-align -Wno-documentation -Wno-shadow -Wno-conversion -Wno-newline-eof -Wno-header-hygiene -Wno-switch-enum -Wno-undefined-reinterpret-cast -Wno-variadic-macros -Wno-gnu-zero-variadic-macro-arguments -Wno-overloaded-virtual -Wno-dynamic-class-memaccess -Wno-nested-anon-types -Wno-invalid-offsetof -Wno-reorder -Wno-local-type-template-args -Wno-unreachable-code -Wno-unreachable-code-return -Wno-format-pedantic -Wno-unused-private-field -Wno-unused-parameter -Wno-unused-member-function -Wno-used-but-marked-unused -Wno-unused-variable -Wno-format-nonliteral -Wno-shift-sign-overflow -Wno-comma -Wno-expansion-to-defined -Wno-undefined-func-template -Wno-weak-template-vtables -Wno-double-promotion -Wno-nonportable-include-path -Wno-disabled-macro-expansion -Wno-missing-noreturn -Wno-dollar-in-identifier-extension -Wno-return-type-c-linkage -Wno-shadow-field -Wno-unused-template -Wno-c++2a-compat -Wno-tautological-type-limit-compare -Wno-c++98-compat")
SET(CMAKE_STATIC_LIBRARY_PREFIX "")

SET(PHYSX_HTML5_COMPILE_DEFS _LIB;_CRT_SECURE_NO_DEPRECATE;_CRT_NONSTDC_NO_DEPRECATE;PX_PHYSX_STATIC_LIB)
SET(PHYSX_HTML5_DEBUG_COMPILE_DEFS _DEBUG;PX_DEBUG=1;PX_CHECKED=1;PX_SUPPORT_PVD=1)
SET(PHYSX_HTML5_CHECKED_COMPILE_DEFS NDEBUG;PX_CHECKED=1;PX_SUPPORT_PVD=1)
SET(PHYSX_HTML5_PROFILE_COMPILE_DEFS NDEBUG;PX_PROFILE=1;PX_SUPPORT_PVD=1)
SET(PHYSX_HTML5_RELEASE_COMPILE_DEFS NDEBUG;PX_SUPPORT_PVD=0)

SET(CMAKE_DEBUG_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_PROFILE_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_CHECKED_POSTFIX $ENV{LIB_SUFFIX})
SET(CMAKE_RELEASE_POSTFIX $ENV{LIB_SUFFIX})

ADD_DEFINITIONS(-DPX_SIMD_DISABLED)

# Needed so FIND_PACKAGE works
SET(CMAKE_FIND_ROOT_PATH  "")

FIND_PACKAGE(PxShared REQUIRED)

# Include the PxShared-exports.cmake file that PxShared created. There's other ways to do this, but there's a bunch of issues with it in a CI environment
#  such as - we don't want to be installing any built artifacts on this server, etc. So for now we hack it.

# Add PxShared as a dependency so that we can use project references
ADD_SUBDIRECTORY(${PXSHARED_ROOT_DIR}/src/compiler/cmake/html5 "${CMAKE_CURRENT_BINARY_DIR}/pxshared_bin")

# Include all of the projects
INCLUDE(LowLevel.cmake)
INCLUDE(LowLevelAABB.cmake)
INCLUDE(LowLevelCloth.cmake)
INCLUDE(LowLevelDynamics.cmake)
INCLUDE(LowLevelParticles.cmake)
INCLUDE(PhysX.cmake)
INCLUDE(PhysXCharacterKinematic.cmake)
INCLUDE(PhysXCommon.cmake)
INCLUDE(PhysXCooking.cmake)
INCLUDE(PhysXExtensions.cmake)
INCLUDE(PhysXVehicle.cmake)
INCLUDE(SceneQuery.cmake)
INCLUDE(SimulationController.cmake)

