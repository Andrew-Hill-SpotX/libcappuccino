cmake_minimum_required(VERSION 2.8)
project(cappuccino_examples CXX)

### fifo_simple ###
project(cap_fifo_simple CXX)
set(FIFO_SIMPLE_SOURCE_FILES
    fifo_simple.cpp
)
add_executable(${PROJECT_NAME} ${FIFO_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### lfu_simple ###
project(cap_lfu_simple CXX)
set(LFU_SIMPLE_SOURCE_FILES
    lfu_simple.cpp
)
add_executable(${PROJECT_NAME} ${LFU_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### lfuda_simple ###
project(cap_lfuda_simple CXX)
set(LFUDA_SIMPLE_SOURCE_FILES
    lfuda_simple.cpp
)
add_executable(${PROJECT_NAME} ${LFUDA_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### lru_simple ###
project(cap_lru_simple CXX)
set(LRU_SIMPLE_SOURCE_FILES
    lru_simple.cpp
)
add_executable(${PROJECT_NAME} ${LRU_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### tlru_simple ###
project(cap_tlru_simple CXX)
set(TLRU_SIMPLE_SOURCE_FILES
    tlru_simple.cpp
)
add_executable(${PROJECT_NAME} ${TLRU_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### utlru_simple ###
project(cap_utlru_simple CXX)
set(UTLRU_SIMPLE_SOURCE_FILES
    utlru_simple.cpp
)
add_executable(${PROJECT_NAME} ${UTLRU_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### bench ###
project(cap_bench CXX)
set(TLRU_BENCH_SOURCE_FILES
    bench.cpp
)
add_executable(${PROJECT_NAME} ${TLRU_BENCH_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino pthread)

### mru_simple ###
project(cap_mru_simple CXX)
set(MRU_SIMPLE_SOURCE_FILES
    mru_simple.cpp
)
add_executable(${PROJECT_NAME} ${MRU_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)

### rr_simple ###
project(cap_rr_simple CXX)
set(RR_SIMPLE_SOURCE_FILES
    rr_simple.cpp
)
add_executable(${PROJECT_NAME} ${RR_SIMPLE_SOURCE_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE cappuccino)
