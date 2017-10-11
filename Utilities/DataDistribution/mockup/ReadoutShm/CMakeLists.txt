# @author Gvozden Neskovic
# @brief  cmake setup for shm-readout-STFBuilder mockup

set(MODULE_NAME "MockupReadoutShm")
set(MODULE_BUCKET_NAME O2DeviceApplication_bucket)

O2_SETUP(NAME ${MODULE_NAME})

set(SRCS
		src/ReadoutO2DataModel.cxx
		src/ReadoutDevice.cxx
		src/StfBuilderDevice.cxx
		src/StfHandlerDevice.cxx
	)

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

O2_GENERATE_LIBRARY()

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

Set(Exe_Names
    mockupReadoutDevice
    mockupStfBuilderDevice
    mockupStfHandlerDevice
  )

set(Exe_Source
    src/runReadoutDevice.cxx
    src/runStfBuilderDevice.cxx
    src/runStfHandlerDevice.cxx
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
      BUCKET_NAME ${MODULE_BUCKET_NAME}
      NO_INSTALL TRUE
  )
EndForEach (_file RANGE 0 ${_length})
