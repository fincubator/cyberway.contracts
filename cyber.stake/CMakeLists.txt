add_contract(cyber.stake cyber.stake ${CMAKE_CURRENT_SOURCE_DIR}/src/cyber.stake.cpp)
target_include_directories(cyber.stake.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include
   ${CMAKE_CURRENT_SOURCE_DIR}/../cyber.token/include
   ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(cyber.stake.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
