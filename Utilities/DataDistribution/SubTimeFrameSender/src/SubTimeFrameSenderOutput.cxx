// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.


#include "SubTimeFrameSender/SubTimeFrameSenderOutput.h"
#include "SubTimeFrameSender/SubTimeFrameSenderDevice.h"

#include "Common/SubTimeFrameDataModel.h"
#include "Common/SubTimeFrameVisitors.h"

#include <O2Device/O2Device.h>
#include <FairMQDevice.h>
#include <FairMQStateMachine.h>
#include <FairMQLogger.h>

#include <stdexcept>

namespace o2 {
namespace DataDistribution {

void StfSenderOutputInterface::Start(std::uint32_t pEpnCnt)
{
  if (!mDevice.CheckCurrentState(StfSenderDevice::RUNNING)) {
    LOG(WARN) << "Not creating interface threads. StfSenderDevice is not running.";
    return;
  }

  assert(mOutputThreads.size() == 0);

  for (std::uint32_t tid = 0; tid < pEpnCnt; tid++) // tid matches output channel index (EPN idx)
    mOutputThreads.emplace_back(std::thread(&StfSenderOutputInterface::DataHandlerThread, this, tid));
}

void StfSenderOutputInterface::Stop()
{
  // Flush all queues
  for (auto &lIdQueue : mStfQueues) {
    lIdQueue.second.flush();
    lIdQueue.second.stop();
  }

  // wait for threads to exit
  for (auto &lIdThread : mOutputThreads)
    lIdThread.join();

  mOutputThreads.clear();
  mStfQueues.clear();
}

/// Receiving thread
void StfSenderOutputInterface::DataHandlerThread(const std::uint32_t pEpnIdx)
{
  auto &lStfQueue = mStfQueues[pEpnIdx];
  auto &lOutputChan = mDevice.GetChannel(mDevice.getOutputChannelName(), pEpnIdx);

  LOG(INFO) << "StfSenderOutput[" << pEpnIdx << "]: Starting the thread";

  while (mDevice.CheckCurrentState(StfSenderDevice::RUNNING)) {

    SubTimeFrame lStf;
    if (!lStfQueue.pop(lStf)) {
      LOG(INFO) << "StfSenderOutput[" << pEpnIdx << "]: STF queue drained";
      break;
    }

    // Choose the serialization method
#if STF_SERIALIZATION == 1
    InterleavedHdrDataSerializer lStfSerializer(lOutputChan);
#elif STF_SERIALIZATION == 2
    HdrDataSerializer lStfSerializer(lOutputChan);
#else
#error "Unknown STF_SERIALIZATION type"
#endif

    try {
      lStfSerializer.serialize(std::move(lStf));
    } catch (std::exception& e) {
      if (mDevice.CheckCurrentState(StfSenderDevice::RUNNING))
        LOG(ERROR) << "StfSenderOutput[" << pEpnIdx << "]: exception on send: " << e.what();
      else
        LOG(INFO) << "StfSenderOutput[" << pEpnIdx << "](NOT RUNNING): exception on send: " << e.what();

      break;
    }
  }

  LOG(INFO) << "StfSenderOutput[" << pEpnIdx << "]: Exiting... ";
}

}
} /* o2::DataDistribution */
