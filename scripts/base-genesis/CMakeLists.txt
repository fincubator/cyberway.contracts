set(GENESIS_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CYBERWAY_CONTRACTS ${CMAKE_BINARY_DIR})

string(TIMESTAMP INITIAL_TIMESTAMP %Y-%m-%dT%H:%M:%S.000 UTC)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/genesis.json.tmpl ${CMAKE_CURRENT_BINARY_DIR}/genesis.json.tmpl @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/genesis-info.json.tmpl ${CMAKE_CURRENT_BINARY_DIR}/genesis-info.json)

file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/genesis.json.tmpl GENESIS_JSON_CONTENT REGEX ^[^\#].*)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/genesis.json ${GENESIS_JSON_CONTENT})

add_custom_target(generate-genesis ALL DEPENDS cyber.bios.wasm cyber.token.wasm cyber.stake.wasm)
add_custom_command(TARGET generate-genesis POST_BUILD COMMAND ${CMAKE_COMMAND} ARGS -P ${CMAKE_CURRENT_SOURCE_DIR}/create-genesis.cmake ${EOSIO_ROOT})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/genesis.dat DESTINATION ${CMAKE_INSTALL_PREFIX}/base-genesis/)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/genesis.json DESTINATION ${CMAKE_INSTALL_PREFIX}/base-genesis/)
