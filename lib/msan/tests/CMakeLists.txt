include(CheckCXXCompilerFlag)
include(CompilerRTCompile)
include(CompilerRTLink)

include_directories(..)
include_directories(../..)

# Instrumented libcxx sources and build flags.
file(GLOB MSAN_LIBCXX_SOURCES ${COMPILER_RT_LIBCXX_PATH}/src/*.cpp)
set(MSAN_LIBCXX_CFLAGS
  -I${COMPILER_RT_LIBCXX_PATH}/include
  -fsanitize=memory
  -fsanitize-memory-track-origins
  -fPIC
  -Wno-\#warnings
  -Wno-pedantic
  -g
  -O2
  -fstrict-aliasing
  -fno-exceptions
  -nostdinc++
  -fno-omit-frame-pointer
  -mno-omit-leaf-frame-pointer)
set(MSAN_LIBCXX_LINK_FLAGS
  -nodefaultlibs
  -lrt
  -lc
  -lstdc++
  -fsanitize=memory)
append_if(COMPILER_RT_HAS_LIBPTHREAD -lpthread MSAN_LIBCXX_LINK_FLAGS)

# Unittest sources and build flags.
set(MSAN_UNITTEST_SOURCES msan_test.cc msan_test_main.cc)
set(MSAN_LOADABLE_SOURCE msan_loadable.cc)
set(MSAN_UNITTEST_HEADERS
  msan_test_config.h
  msandr_test_so.h
  ../../../include/sanitizer/msan_interface.h
)
set(MSANDR_UNITTEST_SOURCE msandr_test_so.cc)
set(MSAN_UNITTEST_COMMON_CFLAGS
  -I${COMPILER_RT_LIBCXX_PATH}/include
  ${COMPILER_RT_GTEST_CFLAGS}
  -I${COMPILER_RT_SOURCE_DIR}/include
  -I${COMPILER_RT_SOURCE_DIR}/lib
  -I${COMPILER_RT_SOURCE_DIR}/lib/msan
  -stdlib=libc++
  -g
  -O2
  -fno-exceptions
  -fno-omit-frame-pointer
  -mno-omit-leaf-frame-pointer
  -Wno-deprecated-declarations
  -Wno-unused-variable
  -Wno-zero-length-array
  -Werror=sign-compare
)
set(MSAN_UNITTEST_INSTRUMENTED_CFLAGS
  ${MSAN_UNITTEST_COMMON_CFLAGS}
  -fsanitize=memory
  -fsanitize-memory-track-origins
  -mllvm -msan-keep-going=1
)
set(MSAN_UNITTEST_LINK_FLAGS
  -fsanitize=memory
  # FIXME: we build libcxx without cxxabi and need libstdc++ to provide it.
  -lstdc++
)
append_if(COMPILER_RT_HAS_LIBDL -ldl MSAN_UNITTEST_LINK_FLAGS)
set(MSAN_LOADABLE_LINK_FLAGS
  -fsanitize=memory
  -shared
)

# Compile source for the given architecture, using compiler
# options in ${ARGN}, and add it to the object list.
macro(msan_compile obj_list source arch)
  get_filename_component(basename ${source} NAME)
  set(output_obj "${basename}.${arch}.o")
  get_target_flags_for_arch(${arch} TARGET_CFLAGS)
  set(COMPILE_DEPS ${MSAN_UNITTEST_HEADERS})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND COMPILE_DEPS gtest msan)
  endif()
  clang_compile(${output_obj} ${source}
                CFLAGS ${ARGN} ${TARGET_CFLAGS}
                DEPS ${COMPILE_DEPS})
  list(APPEND ${obj_list} ${output_obj})
endmacro()

macro(msan_link_shared so_list so_name arch)
  parse_arguments(SOURCE "OBJECTS;LINKFLAGS;DEPS" "" ${ARGN})
  set(output_so "${CMAKE_CURRENT_BINARY_DIR}/${so_name}.${arch}.so")
  get_target_flags_for_arch(${arch} TARGET_LINKFLAGS)
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND SOURCE_DEPS msan)
  endif()
  clang_link_shared(${output_so}
                OBJECTS ${SOURCE_OBJECTS}
                LINKFLAGS ${TARGET_LINKFLAGS} ${SOURCE_LINKFLAGS}
                DEPS ${SOURCE_DEPS})
  list(APPEND ${so_list} ${output_so})
endmacro()

# Link MSan unit test for a given architecture from a set
# of objects in ${ARGN}.
macro(add_msan_test test_suite test_name arch)
  get_target_flags_for_arch(${arch} TARGET_LINK_FLAGS)
  set(TEST_DEPS ${ARGN} ${MSAN_LOADABLE_SO})
  if(NOT COMPILER_RT_STANDALONE_BUILD)
    list(APPEND TEST_DEPS msan)
  endif()
  add_compiler_rt_test(${test_suite} ${test_name}
                       OBJECTS ${ARGN}
                       DEPS ${TEST_DEPS}
                       LINK_FLAGS ${MSAN_UNITTEST_LINK_FLAGS}
                                  ${TARGET_LINK_FLAGS}
                                  "-Wl,-rpath=${CMAKE_CURRENT_BINARY_DIR}")
endmacro()

# Main MemorySanitizer unit tests.
add_custom_target(MsanUnitTests)
set_target_properties(MsanUnitTests PROPERTIES FOLDER "MSan unit tests")

# Adds MSan unit tests and benchmarks for architecture.
macro(add_msan_tests_for_arch arch)
  # Build gtest instrumented with MSan.
  set(MSAN_INST_GTEST)
  msan_compile(MSAN_INST_GTEST ${COMPILER_RT_GTEST_SOURCE} ${arch} 
                               ${MSAN_UNITTEST_INSTRUMENTED_CFLAGS})

  # Build libcxx instrumented with MSan.
  set(MSAN_INST_LIBCXX_OBJECTS)
  foreach(SOURCE ${MSAN_LIBCXX_SOURCES})
    msan_compile(MSAN_INST_LIBCXX_OBJECTS ${SOURCE} ${arch} 
                 ${MSAN_LIBCXX_CFLAGS})
  endforeach(SOURCE)

  set(MSAN_INST_LIBCXX)
  msan_link_shared(MSAN_INST_LIBCXX "libcxx" ${arch}
                   OBJECTS ${MSAN_INST_LIBCXX_OBJECTS}
                   LINKFLAGS ${MSAN_LIBCXX_LINK_FLAGS}
                   DEPS ${MSAN_INST_LIBCXX_OBJECTS})

  # Instrumented tests.
  set(MSAN_INST_TEST_OBJECTS)
  foreach (SOURCE ${MSAN_UNITTEST_SOURCES})
    msan_compile(MSAN_INST_TEST_OBJECTS ${SOURCE} ${arch}
                 ${MSAN_UNITTEST_INSTRUMENTED_CFLAGS})
  endforeach(SOURCE)

  # Instrumented loadable module objects.
  set(MSAN_INST_LOADABLE_OBJECTS)
  msan_compile(MSAN_INST_LOADABLE_OBJECTS ${MSAN_LOADABLE_SOURCE} ${arch}
               ${MSAN_UNITTEST_INSTRUMENTED_CFLAGS})

  # Uninstrumented shared object for MSanDR tests.
  set(MSANDR_TEST_OBJECTS)
  msan_compile(MSANDR_TEST_OBJECTS ${MSANDR_UNITTEST_SOURCE} ${arch}
               ${MSAN_UNITTEST_COMMON_CFLAGS})

  # Instrumented loadable library tests.
  set(MSAN_LOADABLE_SO)
  msan_link_shared(MSAN_LOADABLE_SO "libmsan_loadable" ${arch}
                   OBJECTS ${MSAN_INST_LOADABLE_OBJECTS}
                   DEPS ${MSAN_INST_LOADABLE_OBJECTS})

  # Uninstrumented shared library tests.
  set(MSANDR_TEST_SO)
  msan_link_shared(MSANDR_TEST_SO "libmsandr_test" ${arch}
                   OBJECTS ${MSANDR_TEST_OBJECTS}
                   DEPS ${MSANDR_TEST_OBJECTS})

  # Link everything together.
  add_msan_test(MsanUnitTests "Msan-${arch}-Test" ${arch}
                ${MSAN_INST_TEST_OBJECTS} ${MSAN_INST_GTEST}
                ${MSAN_INST_LIBCXX} ${MSANDR_TEST_SO})
endmacro()

# We should only build MSan unit tests if we can build instrumented libcxx.
if(COMPILER_RT_CAN_EXECUTE_TESTS AND COMPILER_RT_HAS_LIBCXX_SOURCES)
  if(CAN_TARGET_x86_64)
    add_msan_tests_for_arch(x86_64)
  endif()
endif()
