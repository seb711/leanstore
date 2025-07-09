add_executable(ycsb ycsb/ycsb.cpp)
target_link_libraries(ycsb leanstore Threads::Threads custom_memcmp)
target_link_options(ycsb PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(ycsb PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_library(ycsb_shared SHARED ycsb/ycsb.cpp)
target_link_libraries(ycsb_shared leanstore Threads::Threads custom_memcmp)
target_link_options(ycsb_shared PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(ycsb_shared PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(tpcc tpc-c/tpcc.cpp)
target_link_libraries(tpcc leanstore Threads::Threads custom_memcmp)
target_link_options(tpcc PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(tpcc PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(mixed mixed/mixed.cpp)
target_link_libraries(mixed leanstore Threads::Threads custom_memcmp)
target_link_options(mixed PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(mixed PRIVATE ${SHARED_INCLUDE_DIRECTORY})