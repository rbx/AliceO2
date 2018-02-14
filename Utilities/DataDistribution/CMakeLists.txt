# Remove when API stabilizes
add_definitions(-DSHM_MULTIPART=1)
add_definitions(-DSTF_SERIALIZATION=1)

add_subdirectory (Common)
add_subdirectory (ReadoutEmulator)
add_subdirectory (SubTimeFrameBuilder)
add_subdirectory (SubTimeFrameSender)


configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/startReadoutFLPChain.sh.in
  ${CMAKE_BINARY_DIR}/bin/startReadoutFLPChain.sh)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/readout-flp-chain.json
  ${CMAKE_BINARY_DIR}/bin/config/readout-flp-chain.json)

# Chain configuration and run script for Readout/Emulation mode (no CRU)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/startFLPReadoutDataDistEMU.sh.in
  ${CMAKE_BINARY_DIR}/bin/startFLPReadoutDataDistEMU.sh)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/readout-flp-chain-emu.json
  ${CMAKE_BINARY_DIR}/bin/config/readout-flp-chain-emu.json)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/readout_cfg/readout_emu.cfg
  ${CMAKE_BINARY_DIR}/bin/config/readout_cfg/readout_emu.cfg)
