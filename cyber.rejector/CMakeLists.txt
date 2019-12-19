add_contract(cyber.rejector cyber.rejector ${CMAKE_CURRENT_SOURCE_DIR}/src/cyber.rejector.cpp)
target_include_directories(cyber.rejector.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(cyber.rejector.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
