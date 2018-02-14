# @author Gvozden Neskovic
# @brief  cmake for SubTimeFrameSender

o2_define_bucket(
    NAME
    SubTimeFrameSenderDevice_bucket

    DEPENDENCIES
    DataDistributionCommon
    O2Device
    root_base_bucket Core Gpad Hist
)

set(MODULE_NAME "SubTimeFrameSender")
set(MODULE_BUCKET_NAME SubTimeFrameSenderDevice_bucket)

O2_SETUP(NAME ${MODULE_NAME})

# SubTimeFrameSenderDevice lib

set(SRCS
    src/SubTimeFrameSenderDevice.cxx
)

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

O2_GENERATE_LIBRARY()

set(LIBRARY_NAME ${MODULE_NAME})
set(BUCKET_NAME ${MODULE_BUCKET_NAME})

# SubTimeFrameSenderDevice exe

set(EXE_SRC
    src/runSubTimeFrameSenderDevice.cxx
)

O2_GENERATE_EXECUTABLE(
    EXE_NAME "SubTimeFrameSenderDevice"
    SOURCES ${EXE_SRC}
    MODULE_LIBRARY_NAME ${LIBRARY_NAME}
    BUCKET_NAME ${MODULE_BUCKET_NAME}
)
