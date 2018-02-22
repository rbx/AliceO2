# @author Gvozden Neskovic
# @brief  cmake for TimeFrameBuilder

o2_define_bucket(
    NAME
    TimeFrameBuilderDevice_bucket

    DEPENDENCIES
    DataDistributionCommon
    O2Device
    root_base_bucket Core Gpad Hist
)

set(MODULE_NAME "TimeFrameBuilder")
set(MODULE_BUCKET_NAME TimeFrameBuilderDevice_bucket)

O2_SETUP(NAME ${MODULE_NAME})

# TimeFrameBuilderDevice lib

set(SRCS
    src/TimeFrameBuilderDevice.cxx
    src/TimeFrameBuilderInput.cxx
)

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

O2_GENERATE_LIBRARY()

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

# TimeFrameBuilderDevice exe

set(EXE_SRC
    src/runTimeFrameBuilderDevice.cxx
)

O2_GENERATE_EXECUTABLE(
    EXE_NAME "TimeFrameBuilderDevice"
    SOURCES ${EXE_SRC}
    MODULE_LIBRARY_NAME ${LIBRARY_NAME}
    BUCKET_NAME ${MODULE_BUCKET_NAME}
)
