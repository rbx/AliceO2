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

class FairMQRegionSHM;

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

  void init(FairMQRegionSHM *dataRegion, FairMQRegionSHM *descRegion, size_t sp_size);
  void teardown();

  bool get_superpage(CRUSuperpage &sp);

  // not useful
  void put_superpage(const char *spVirtAddr);

  // address must match shm fairmq messages sent out
  void get_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize);
  void put_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize);

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


class ReadoutO2Interface {

public:
  ReadoutO2Interface();
  ~ReadoutO2Interface();
  void run();
  void stop();

  bool get(ReadoutO2Data &rd);
  void put(ReadoutO2Data &&rd);
  void clear();

private:
  std::deque<ReadoutO2Data>   mO2Data;
  std::mutex                  mLock;
  std::condition_variable     mDataCond;
  bool                        mRunning;
  std::thread                 mRunner;
};


class ReadoutDevice : public Base::O2Device
{
public:
  static constexpr const char* OptionKeyOutputChannelName = "output-channel-name";
  static constexpr const char* OptionKeyReadoutDataRegionName = "data-shm-region-name";
  static constexpr const char* OptionKeyReadoutDataRegionSize = "data-shm-region-size";
  static constexpr const char* OptionKeyReadoutDescRegionName = "desc-shm-region-name";
  static constexpr const char* OptionKeyReadoutDescRegionSize = "desc-shm-region-size";
  static constexpr const char* OptionKeyReadoutSuperpageSize = "cru-superpage-size";

  /// Default constructor
  ReadoutDevice();

  /// Default destructor
  ~ReadoutDevice();

  void InitTask() final;

protected:

  bool ConditionalRun() final;

  std::string      mOutChannelName;
  std::string      mDataRegionName;
  std::size_t      mDataRegionSize;
  std::string      mDescRegionName;
  std::size_t      mDescRegionSize;
  std::size_t      mSuperpageSize;

  FairMQRegionSHM   *mDataRegion = nullptr;
  FairMQRegionSHM   *mDescRegion = nullptr;

  CRUMemoryHandler      mCRUMemoryHandler;
  ReadoutO2Interface    mO2Interface;
};

} } } /* namespace o2::DataDistribution::mockup */

#endif /* ALICEO2_MOCKUP_READOUT_DEVICE_H_ */
