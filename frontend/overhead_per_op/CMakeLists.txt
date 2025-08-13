add_executable(tpcc-neworder tpcc-neworder/tpcc.cpp)
target_link_libraries(tpcc-neworder leanstore Threads::Threads custom_memcmp)
target_link_options(tpcc-neworder PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(tpcc-neworder PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_library(tpcc-newpayment tpcc-newpayment/tpcc.cpp)
target_link_libraries(tpcc-newpayment leanstore Threads::Threads custom_memcmp)
target_link_options(tpcc-newpayment PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(tpcc-newpayment PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(ycsb-insert ycsb-insert/mixed.cpp)
target_link_libraries(ycsb-insert leanstore Threads::Threads custom_memcmp)
target_link_options(ycsb-insert PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(ycsb-insert PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(ycsb-lookup ycsb-lookup/mixed.cpp)
target_link_libraries(ycsb-lookup leanstore Threads::Threads custom_memcmp)
target_link_options(ycsb-lookup PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(ycsb-lookup PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(ycsb-update ycsb-update/mixed.cpp)
target_link_libraries(ycsb-update leanstore Threads::Threads custom_memcmp)
target_link_options(ycsb-update PRIVATE -Wl,-wrap,memcmp  -Wl,-wrap,memcpy -Wl,-wrap,memmove)
target_include_directories(ycsb-update PRIVATE ${SHARED_INCLUDE_DIRECTORY})
