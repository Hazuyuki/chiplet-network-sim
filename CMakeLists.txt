cmake_minimum_required (VERSION 3.8)

project (ChipletNetworkSim VERSION 1.0.1
						   DESCRIPTION "A cycle-accurate packet-parallel network simulator."
                           LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)

if(MSVC)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -DDEBUG")
endif()
message("CMAKE_CXX_FLAGS_DEBUG value is ${CMAKE_CXX_FLAGS_DEBUG}")
message("CMAKE_CXX_FLAGS_RELEASE value is ${CMAKE_CXX_FLAGS_RELEASE}")
message("CMAKE_CXX_FLAGS_RELWITHDEBINFO is ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# Find Boost
find_package(Boost)
if(Boost_FOUND)
include_directories(${Boost_INCLUDE_DIRS})
endif()

aux_source_directory(src SRC_FILES)
aux_source_directory(src/netrace NETRACE_FILES)
aux_source_directory(src/topologies TOPOLOGY_FILES)
aux_source_directory(src/traffic TRAFFIC_FILES)

# Remove test files from main executable (aux_source_directory returns relative paths)
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_nvswitch.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_nvswitch_simple.cpp")

add_executable (${PROJECT_NAME} ${SRC_FILES} ${NETRACE_FILES} ${TOPOLOGY_FILES} ${TRAFFIC_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC src src/netrace src/topologies src/traffic)

if(MSVC)
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:RELWITHDEBINFO>:/PROFILE>)
endif()

# Add test executable for NVSwitch topology
option(BUILD_NVSWITCH_TEST "Build NVSwitch test executable" OFF)
if(BUILD_NVSWITCH_TEST)
  # Create a copy of SRC_FILES without main.cpp
  set(TEST_SRC_FILES ${SRC_FILES})
  list(REMOVE_ITEM TEST_SRC_FILES "src/main.cpp")
  
  add_executable(test_nvswitch 
    src/topologies/test_nvswitch.cpp
    ${TEST_SRC_FILES}
    ${NETRACE_FILES}
    ${TOPOLOGY_FILES}
    ${TRAFFIC_FILES}
  )
  target_include_directories(test_nvswitch PUBLIC src src/netrace src/topologies src/traffic)
  if(Boost_FOUND)
    target_link_libraries(test_nvswitch ${Boost_LIBRARIES})
  endif()
  
  # Add simple test
  add_executable(test_nvswitch_simple 
    src/topologies/test_nvswitch_simple.cpp
    ${TEST_SRC_FILES}
    ${NETRACE_FILES}
    ${TOPOLOGY_FILES}
    ${TRAFFIC_FILES}
  )
  target_include_directories(test_nvswitch_simple PUBLIC src src/netrace src/topologies src/traffic)
  if(Boost_FOUND)
    target_link_libraries(test_nvswitch_simple ${Boost_LIBRARIES})
  endif()
endif()