IF(NOT DEFINED ENV{ZOOKEEPERPATH})
  MESSAGE(FATAL_ERROR "You did not define the environment variable ZOOKEEPERPATH which is needed to find Zookeeper. Please set this variable and execute cmake again.")
ENDIF(NOT DEFINED ENV{ZOOKEEPERPATH})

SET(ZOOKEEPERPATH $ENV{ZOOKEEPERPATH})

set(INCLUDE_DIRECTORIES
  ${BASE_INCLUDE_DIRECTORIES}
  ${Boost_INCLUDE_DIR}
  ${CMAKE_SOURCE_DIR}/devices/flp2epn-zoo
  ${ZOOKEEPERPATH}/src/c/include
  ${ZOOKEEPERPATH}/src/c/generated
)

include_directories(${INCLUDE_DIRECTORIES})

configure_file(
  ${CMAKE_SOURCE_DIR}/devices/flp2epn-zoo/run/startFLP2EPN-zoo.sh.in ${CMAKE_BINARY_DIR}/bin/startFLP2EPN-zoo.sh
)

set(LINK_DIRECTORIES
  ${Boost_LIBRARY_DIRS}
  ${FAIRROOT_LIBRARY_DIR}
  ${ZOOKEEPERPATH}/src/c/.libs/
)

link_directories(${LINK_DIRECTORIES})

set(SRCS
  FLPex.cxx
  EPNex.cxx
  EpnScheduler.cxx
)

set(DEPENDENCIES
  ${DEPENDENCIES}
  ${CMAKE_THREAD_LIBS_INIT}
  boost_serialization boost_date_time boost_thread boost_timer boost_system boost_program_options zookeeper_mt FairMQ
)

set(LIBRARY_NAME FLP2EPNex_zoo)

GENERATE_LIBRARY()

Set(Exe_Names
  testFLP_zoo
  testEPN_zoo
  epnMaster
)

set(Exe_Source
  run/runFLP_zoo.cxx
  run/runEPN_zoo.cxx
  epnMaster.cxx
)

list(LENGTH Exe_Names _length)
math(EXPR _length ${_length}-1)

ForEach(_file RANGE 0 ${_length})
  list(GET Exe_Names ${_file} _name)
  list(GET Exe_Source ${_file} _src)
  set(EXE_NAME ${_name})
  set(SRCS ${_src})
  set(DEPENDENCIES FLP2EPNex_zoo)
  GENERATE_EXECUTABLE()
EndForEach(_file RANGE 0 ${_length})
