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
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_nvswitch_topology.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_credit_flow_control.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_packet_spraying.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_throughput_bottleneck.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_rtt_deviation_diagnosis.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_allreduce_diagnosis.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_link_bottleneck_diagnosis.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_packet_spraying_verify.cpp")
list(REMOVE_ITEM TOPOLOGY_FILES "src/topologies/test_gpu_18port_usage.cpp")

add_executable (${PROJECT_NAME} ${SRC_FILES} ${NETRACE_FILES} ${TOPOLOGY_FILES} ${TRAFFIC_FILES})
target_include_directories(${PROJECT_NAME} PUBLIC src src/netrace src/topologies src/traffic)

if(MSVC)
target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:RELWITHDEBINFO>:/PROFILE>)
endif()

# NVSwitch 拓扑测试程序（默认构建，用于验证拓扑正确性）
set(SRC_FILES_TOPOLOGY_TEST ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_TOPOLOGY_TEST "src/main.cpp")
add_executable(test_nvswitch_topology
  src/topologies/test_nvswitch_topology.cpp
  ${SRC_FILES_TOPOLOGY_TEST}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_nvswitch_topology PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_nvswitch_topology ${Boost_LIBRARIES})
endif()

# Credit 流控测试程序（默认构建）
set(SRC_FILES_CREDIT_TEST ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_CREDIT_TEST "src/main.cpp")
add_executable(test_credit_flow_control
  src/topologies/test_credit_flow_control.cpp
  ${SRC_FILES_CREDIT_TEST}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_credit_flow_control PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_credit_flow_control ${Boost_LIBRARIES})
endif()

# 包泼洒测试程序
set(SRC_FILES_SPRAYING_TEST ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_SPRAYING_TEST "src/main.cpp")
add_executable(test_packet_spraying
  src/topologies/test_packet_spraying.cpp
  ${SRC_FILES_SPRAYING_TEST}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_packet_spraying PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_packet_spraying ${Boost_LIBRARIES})
endif()

# 吞吐瓶颈诊断测试
set(SRC_FILES_BOTTLENECK_TEST ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_BOTTLENECK_TEST "src/main.cpp")
add_executable(test_throughput_bottleneck
  src/topologies/test_throughput_bottleneck.cpp
  ${SRC_FILES_BOTTLENECK_TEST}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_throughput_bottleneck PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_throughput_bottleneck ${Boost_LIBRARIES})
endif()

# RTT 偏差诊断测试
set(SRC_FILES_RTT_DIAG ${SRC_FILES})
list(REMOVE_ITEM SRC_FILES_RTT_DIAG "src/main.cpp")
add_executable(test_rtt_deviation_diagnosis
  src/topologies/test_rtt_deviation_diagnosis.cpp
  ${SRC_FILES_RTT_DIAG}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_rtt_deviation_diagnosis PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_rtt_deviation_diagnosis ${Boost_LIBRARIES})
endif()

# All-Reduce 饱和点诊断
add_executable(test_allreduce_diagnosis
  src/topologies/test_allreduce_diagnosis.cpp
  ${SRC_FILES_RTT_DIAG}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_allreduce_diagnosis PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_allreduce_diagnosis ${Boost_LIBRARIES})
endif()

# 链路瓶颈诊断：按链路类型统计「出链路忙」导致的阻塞次数
add_executable(test_link_bottleneck_diagnosis
  src/topologies/test_link_bottleneck_diagnosis.cpp
  ${SRC_FILES_RTT_DIAG}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_link_bottleneck_diagnosis PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_link_bottleneck_diagnosis ${Boost_LIBRARIES})
endif()

# 包泼洒验证：统计到达每个 GPU 的包来自哪个 Leaf，验证多 Leaf 汇聚是否生效
add_executable(test_packet_spraying_verify
  src/topologies/test_packet_spraying_verify.cpp
  ${SRC_FILES_RTT_DIAG}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_packet_spraying_verify PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_packet_spraying_verify ${Boost_LIBRARIES})
endif()

# GPU 18 口使用实验：到达 GPU 的包从哪个 port(0..17) 进入，验证 18 口是否用满
add_executable(test_gpu_18port_usage
  src/topologies/test_gpu_18port_usage.cpp
  ${SRC_FILES_RTT_DIAG}
  ${NETRACE_FILES}
  ${TOPOLOGY_FILES}
  ${TRAFFIC_FILES}
)
target_include_directories(test_gpu_18port_usage PUBLIC src src/netrace src/topologies src/traffic)
if(Boost_FOUND)
  target_link_libraries(test_gpu_18port_usage ${Boost_LIBRARIES})
endif()

# Add test executable for NVSwitch topology (full test suite, optional)
option(BUILD_NVSWITCH_TEST "Build NVSwitch full test executable" OFF)
if(BUILD_NVSWITCH_TEST)
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