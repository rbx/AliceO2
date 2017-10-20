// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_MOCKUP_READOUT_DEVICE_H_
#define ALICEO2_MOCKUP_READOUT_DEVICE_H_

#include "O2Device/O2Device.h"
#include "ReadoutShm/ReadoutO2DataModel.h"

#include <stack>
#include <deque>
#include <mutex>
#include <condition_variable>

namespace o2 { namespace DataDistribution { namespace mockup {

struct RawDmaPacketDesc {
  uint64_t  mHBFrameID;
  size_t    mRawDataSize;
  bool      mValidHBF;
};

/* RawDataHeader */
struct RDH {
  std::uint64_t   mRDH[4];
};

struct CRUSuperpage {
  size_t  mDataRegionOffset;
  char    *mDataVirtualAddress;
  char    *mDataBusAddress;

  size_t  mDescRegionOffset;
  char    *mDescVirtualAddress;
  char    *mDescBusAddress;
};

class CRUMemoryHandler {
public:

  CRUMemoryHandler() = default;
  ~CRUMemoryHandler() { teardown(); }

  void init(FairMQUnmanagedRegion *dataRegion, FairMQUnmanagedRegion *descRegion, size_t sp_size);
  void teardown();

  bool get_superpage(CRUSuperpage &sp);

  // not useful
  void put_superpage(const char *spVirtAddr);

  // address must match shm fairmq messages sent out
  void get_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize);
  void put_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize);
  size_t free_superpages();

private:
  std::size_t                                               mSuperpageSize;
  std::mutex                                                mLock;
  std::stack<CRUSuperpage>                                  mSuperpages; /* stack: use hot buffers */
  std::map<const char*, CRUSuperpage>                       mVirtToSuperpage;

  // map<sp_address, map<buff_addr, buf_len>>
  std::map<const char*, std::map<const char*, std::size_t>> mUsedSuperPages;

  char                                                      *mDataStartAddress;
  std::size_t                                               mDataRegionSize;
};

class ReadoutDevice : public Base::O2Device
{
public:
  static constexpr const char* OptionKeyOutputChannelName = "output-channel-name";
  static constexpr const char* OptionKeyFreeShmChannelName = "free-shm-channel-name";
  static constexpr const char* OptionKeyReadoutDataRegionSize = "data-shm-region-size";
  static constexpr const char* OptionKeyReadoutDescRegionSize = "desc-shm-region-size";
  static constexpr const char* OptionKeyReadoutSuperpageSize = "cru-superpage-size";

  /// Default constructor
  ReadoutDevice();

  /// Default destructor
  ~ReadoutDevice();

  void InitTask() final;

protected:

  bool ConditionalRun() final;
  void PreRun() final;
  void PostRun() final;

  void FreeShmThread();

  std::string      mOutChannelName;
  std::string      mFreeShmChannelName;
  std::size_t      mDataRegionSize;
  std::size_t      mDescRegionSize;
  std::size_t      mSuperpageSize;

  FairMQUnmanagedRegionPtr  mDataRegion = nullptr;
  FairMQUnmanagedRegionPtr  mDescRegion = nullptr;

  CRUMemoryHandler      mCRUMemoryHandler;
  std::thread           mFreeShmThread;
};

} } } /* namespace o2::DataDistribution::mockup */

#endif /* ALICEO2_MOCKUP_READOUT_DEVICE_H_ */
