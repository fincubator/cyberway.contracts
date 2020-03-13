execute_process(COMMAND ${CMAKE_ARGV3}/bin/create-genesis -g ${CMAKE_CURRENT_BINARY_DIR}/genesis-info.json -o ${CMAKE_CURRENT_BINARY_DIR}/genesis.dat OUTPUT_VARIABLE GENERATOR_OUT)
message(${CMAKE_ARGV3}/bin/create-genesis -g ${CMAKE_CURRENT_BINARY_DIR}/genesis-info.json -o ${CMAKE_CURRENT_BINARY_DIR}/genesis.dat)
file(SHA256 ${CMAKE_CURRENT_BINARY_DIR}/genesis.dat GENESIS_DATA_HASH)
configure_file(${CMAKE_CURRENT_BINARY_DIR}/genesis.json ${CMAKE_CURRENT_BINARY_DIR}/genesis.json)
