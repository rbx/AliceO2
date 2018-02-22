// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_STF_SENDER_OUTPUT_H_
#define ALICEO2_STF_SENDER_OUTPUT_H_

#include "Common/SubTimeFrameDataModel.h"
#include "Common/ConcurrentQueue.h"

#include <vector>
#include <map>
#include <thread>

namespace o2 {
namespace DataDistribution {

class StfSenderDevice;

class StfSenderOutputInterface {
public:

  StfSenderOutputInterface() = default;
  StfSenderOutputInterface(StfSenderDevice &pStfSenderDev)
  : mDevice(pStfSenderDev) {}

  void Start(std::uint32_t pCnt);
  void Stop();

  void DataHandlerThread(const std::uint32_t pEpnIdx);

  void PushStf(const std::uint32_t pEpnIdx, SubTimeFrame &&pStf) {
    assert(pEpnIdx < mOutputThreads.size());
    mStfQueues[pEpnIdx].push(std::move(pStf));
  }

private:
  /// Ref to the main SubTimeBuilder O2 device
  StfSenderDevice &mDevice;

  /// Threads for output channels (to EPNs)
  std::vector<std::thread> mOutputThreads;

  /// Outstanding queues of STFs per EPN
  std::map<std::uint32_t, ConcurrentFifo<SubTimeFrame> > mStfQueues;
};

}
} /* namespace o2::DataDistribution */

#endif /* ALICEO2_STF_SENDER_OUTPUT_H_ */
