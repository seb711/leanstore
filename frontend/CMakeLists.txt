add_executable(frontend frontend.cpp)
add_dependencies(frontend leanstore)
target_link_libraries(frontend leanstore)


add_executable(ycsb ycsb/ycsb.cpp)
target_link_libraries(ycsb leanstore Threads::Threads)
target_include_directories(ycsb PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(tpcc tpc-c/tpcc.cpp)
target_link_libraries(tpcc leanstore Threads::Threads )
target_include_directories(tpcc PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_executable(iob iob/iob.cpp)
target_link_libraries(iob leanstore Threads::Threads)
target_include_directories(iob PRIVATE ${SHARED_INCLUDE_DIRECTORY})

# these are not really needed any more due to newer version of osv apps

add_library(ycsb_lib SHARED ycsb/ycsb.cpp)
target_link_libraries(ycsb_lib leanstore Threads::Threads)
target_include_directories(ycsb_lib PRIVATE ${SHARED_INCLUDE_DIRECTORY})

add_library(tpcc_lib SHARED tpc-c/tpcc.cpp)
target_link_libraries(tpcc_lib leanstore Threads::Threads )
target_include_directories(tpcc_lib PRIVATE ${SHARED_INCLUDE_DIRECTORY})
