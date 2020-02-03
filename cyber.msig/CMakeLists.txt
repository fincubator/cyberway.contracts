add_contract(cyber.msig cyber.msig ${CMAKE_CURRENT_SOURCE_DIR}/src/cyber.msig.cpp)
install_contract(cyber.msig)

target_include_directories(cyber.msig.wasm
   PUBLIC 
   ${CMAKE_CURRENT_SOURCE_DIR}/include)

set_target_properties(cyber.msig.wasm
   PROPERTIES
   RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
