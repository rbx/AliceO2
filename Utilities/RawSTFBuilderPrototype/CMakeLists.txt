configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/raw-stf-prototype.json ${CMAKE_BINARY_DIR}/bin/config/raw-stf-prototype.json)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/run/startPrototype.sh.in ${CMAKE_BINARY_DIR}/bin/startPrototype.sh)

set(MODULE_NAME "RawSTFBuilderPrototype")

O2_SETUP(NAME ${MODULE_NAME})

set(SRCS
  src/RawSTFBuilder.cxx
  src/RawSTFSink.cxx
)

set(HEADERS
  include/${MODULE_NAME}/RawSTFBuilder.h
  include/${MODULE_NAME}/RawSTFSink.h
)

set(BUCKET_NAME raw_stf_builder_prototype)
set(LIBRARY_NAME ${MODULE_NAME})

O2_GENERATE_LIBRARY()

Set(Exe_Names
  ${Exe_Names}
  rawSTFBuilder
  rawSTFSink
)

set(Exe_Source
  run/runRawSTFBuilder.cxx
  run/runRawSTFSink.cxx
)

list(LENGTH Exe_Names _length)
math(EXPR _length ${_length}-1)

ForEach (_file RANGE 0 ${_length})
list(GET Exe_Names ${_file} _name)
list(GET Exe_Source ${_file} _src)
O2_GENERATE_EXECUTABLE(
  EXE_NAME ${_name}
  SOURCES ${_src}
  MODULE_LIBRARY_NAME ${LIBRARY_NAME}
  BUCKET_NAME ${BUCKET_NAME}
)
EndForEach (_file RANGE 0 ${_length})
