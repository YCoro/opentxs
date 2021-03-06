# Copyright (c) Monetas AG, 2014

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PEDANTIC_CXX_FLAGS}")

set(dependency_include_dir
  ${CMAKE_CURRENT_SOURCE_DIR}/../deps/
)

include_directories(SYSTEM
  ${dependency_include_dir}
  ${dependency_include_dir}/ChaiScript/include
  ${ZMQ_INCLUDE_DIRS}
)

include_directories(
  ${CMAKE_CURRENT_BINARY_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}
)

if(LIBBITCOIN_EXPORT)
  include_directories(SYSTEM ${LIBBITCOIN_INCLUDE_DIRS})
endif()

if(KEYRING_GNOME)
  set(keyring libgnome-keyring.so)
endif()

add_subdirectory(storage)
add_subdirectory(core)
add_subdirectory(consensus)
add_subdirectory(ext)
add_subdirectory(network)
add_subdirectory(client)
add_subdirectory(api)
add_subdirectory(server)
add_subdirectory(contact)
add_subdirectory(ui)
add_subdirectory(util)

if(TREZORCRYPTO_EXPORT)
  set(trezor $<TARGET_OBJECTS:TrezorCrypto>)
else()
  set(trezor "")
endif()

if(CASH_LUCRE_EXPORT)
  set(lucre $<TARGET_OBJECTS:lucre>)
  include_directories(SYSTEM ${dependency_include_dir}/lucre/include)
else()
  set(lucre "")
endif()

if(OT_CASH_EXPORT)
  set(cash $<TARGET_OBJECTS:opentxs-cash>)
  add_subdirectory(cash)
else()
  set(cash "")
endif()

set(object-deps
  $<TARGET_OBJECTS:base64>
  $<TARGET_OBJECTS:irrxml>
  ${lucre}
  ${trezor}
  $<TARGET_OBJECTS:otprotob>
  $<TARGET_OBJECTS:opentxs-api-client>
  $<TARGET_OBJECTS:opentxs-api-crypto>
  $<TARGET_OBJECTS:opentxs-api-network>
  $<TARGET_OBJECTS:opentxs-api-storage>
  $<TARGET_OBJECTS:opentxs-api>
  ${cash}
  $<TARGET_OBJECTS:opentxs-client>
  $<TARGET_OBJECTS:opentxs-consensus>
  $<TARGET_OBJECTS:opentxs-core-contract-basket>
  $<TARGET_OBJECTS:opentxs-core-contract-peer>
  $<TARGET_OBJECTS:opentxs-core-contract>
  $<TARGET_OBJECTS:opentxs-core-crypto>
  $<TARGET_OBJECTS:opentxs-core-transaction>
  $<TARGET_OBJECTS:opentxs-core-util>
  $<TARGET_OBJECTS:opentxs-core>
  $<TARGET_OBJECTS:opentxs-cron>
  $<TARGET_OBJECTS:opentxs-ext>
  $<TARGET_OBJECTS:opentxs-network-zeromq>
  $<TARGET_OBJECTS:opentxs-network>
  $<TARGET_OBJECTS:opentxs-recurring>
  $<TARGET_OBJECTS:opentxs-script>
  $<TARGET_OBJECTS:opentxs-server>
  $<TARGET_OBJECTS:opentxs-storage>
  $<TARGET_OBJECTS:opentxs-storage-drivers>
  $<TARGET_OBJECTS:opentxs-storage-tree>
  $<TARGET_OBJECTS:opentxs-trade>
  $<TARGET_OBJECTS:opentxs-contact>
  $<TARGET_OBJECTS:opentxs-ui>
  $<TARGET_OBJECTS:opentxs-util>
)

set(cxx-install-headers
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Exclusive.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Forward.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/opentxs.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/OT.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Pimpl.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Proto.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Shared.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/SharedPimpl.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/../include/opentxs/Types.hpp"
)

set(cxx-headers
  ${cxx-install-headers}
  "${CMAKE_CURRENT_SOURCE_DIR}/Internal.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Exclusive.tpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Shared.tpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/stdafx.hpp"
)

set(MODULE_NAME opentxs)

add_library(${MODULE_NAME} SHARED
  ${cxx-headers}
  ${object-deps}
  ${CMAKE_CURRENT_SOURCE_DIR}/Factory.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/OT.cpp
)

add_library(${MODULE_NAME}_static STATIC
  ${cxx-headers}
  ${object-deps}
)

if(NOT WIN32)
  set_target_properties(${MODULE_NAME}_static PROPERTIES OUTPUT_NAME ${MODULE_NAME})
endif()

target_link_libraries(${MODULE_NAME}
  PRIVATE
  Threads::Threads
  ${OPENSSL_LIBRARIES}
  ${OPENTXS_PROTO_LIBRARIES}
  ${PROTOBUF_LITE_LIBRARIES}
  ${ZLIB_LIBRARY}
  ${SODIUM_LIBRARY}
  ${OPENTXS_SYSTEM_LIBRARIES}
  ${keyring}
  ${CMAKE_DL_LIBS}
  ${ZMQ_LIBRARIES}
)

if (LIBSECP256K1_EXPORT)
  if (OT_INTERNAL_LIBSECP)
    add_library(staticlibsecp256k1 STATIC IMPORTED)
    set_property(
        TARGET staticlibsecp256k1
        PROPERTY IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/deps/lib/libsecp256k1.a)
    target_link_libraries(${MODULE_NAME} PRIVATE staticlibsecp256k1)
    target_link_libraries(${MODULE_NAME} PUBLIC ${GMP_LIBRARIES})
  else()
    target_link_libraries(${MODULE_NAME} PRIVATE ${LIBSECP256K1_LIBRARY})
  endif()
endif()

if (LIBBITCOIN_EXPORT)
  target_link_libraries(${MODULE_NAME} PRIVATE ${LIBBITCOIN_LIBRARIES})
endif()

if(OT_DHT)
  target_link_libraries(${MODULE_NAME} PRIVATE ${OPENDHT_LIBRARIES} ${GNUTLS_LIBRARIES})
endif()

if (OT_STORAGE_SQLITE)
    target_link_libraries(${MODULE_NAME} PRIVATE ${SQLITE3_LIBRARIES})
endif()

if (OT_STORAGE_FS)
    target_link_libraries(${MODULE_NAME} PRIVATE ${Boost_SYSTEM_LIBRARIES} ${Boost_FILESYSTEM_LIBRARIES} ${Boost_IOSTREAMS_LIBRARIES})
endif()

if(WIN32)
  add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/lib/$<CONFIGURATION>/${MODULE_NAME}.dll"
    "${CMAKE_BINARY_DIR}/bin/$<CONFIGURATION>/${MODULE_NAME}.dll")

  add_custom_command(TARGET ${MODULE_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_BINARY_DIR}/lib/$<CONFIGURATION>/${MODULE_NAME}.pdb"
    "${CMAKE_BINARY_DIR}/bin/$<CONFIGURATION>/${MODULE_NAME}.pdb")
endif()

if(NOT WIN32)
  include(GNUInstallDirs)
  install(TARGETS ${MODULE_NAME} ${MODULE_NAME}_static
          DESTINATION ${CMAKE_INSTALL_LIBDIR}
          EXPORT opentxsTargets
          COMPONENT main)
endif()

install(FILES ${cxx-install-headers}
        DESTINATION include/opentxs
        COMPONENT dev)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}../include/opentxs/interface
        DESTINATION include/opentxs/interface
        COMPONENT dev
        FILES_MATCHING PATTERN "*.hpp")

