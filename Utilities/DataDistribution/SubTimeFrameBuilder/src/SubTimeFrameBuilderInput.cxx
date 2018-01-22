// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.


#include "SubTimeFrameBuilder/SubTimeFrameBuilderInput.h"
#include "SubTimeFrameBuilder/SubTimeFrameBuilderDevice.h"

#include <O2Device/O2Device.h>
#include <FairMQDevice.h>
#include <FairMQStateMachine.h>
#include <FairMQLogger.h>

namespace o2 {
namespace DataDistribution {

void StfInputInterface::Start(unsigned pCnt)
{
  if (!mDevice.CheckCurrentState(StfBuilderDevice::RUNNING)) {
    LOG(WARN) << "Not creating interface threads. StfBuilder is not running.";
    return;
  }

  assert(mInputThreads.size() == 0);

  for (auto tid = 0; tid < pCnt; tid++) // tid matches input channel index
    mInputThreads.emplace_back(std::thread(&StfInputInterface::DataHandlerThread, this, tid));
}

void StfInputInterface::Stop()
{
  for (auto &lIdThread : mInputThreads)
    lIdThread.join();
}

/// Receiving thread
void StfInputInterface::DataHandlerThread(const unsigned pInputChannelIdx)
{
  int ret;
  // current TF Id
  std::uint64_t lCurrentStfId = -1;
  // current  HBFrames collection
  SubTimeFrame lCurrentStf(StfBuilderDevice::gStfOutputChanId, 0);

  // Equipment ID for the HBFrames (from the header)
  ReadoutSubTimeframeHeader lReadoutHdr = { 0 };
  // Number of HBFrame messages to receive
  unsigned lHBFramesToReceive = 0;
  // HBFrames collection
  std::vector<FairMQMessagePtr> lHBFrameMsgs;
  lHBFrameMsgs.reserve(1024);

  // Reference to the input channel
  auto &lInputChan = mDevice.GetChannel(mDevice.getInputChannelName(), pInputChannelIdx);

  while (mDevice.CheckCurrentState(StfBuilderDevice::RUNNING)) {
    try
    {
      // receive channel object info
      FairMQMessagePtr lHdrMsg = lInputChan.NewMessage();

      if ((ret = lInputChan.Receive(lHdrMsg)) < 0)
        throw std::runtime_error("StfHeader receive failed (err = " + std::to_string(ret) + ")");

      // Copy to avoid surprises. The receiving header is not O2 compatible and can be discarded

      assert(lHdrMsg->GetSize() == sizeof(ReadoutSubTimeframeHeader));
      std::memcpy(&lReadoutHdr, lHdrMsg->GetData(), sizeof(ReadoutSubTimeframeHeader));

      // LOG(DEBUG) << "RECEIVED::Header::size: " << lHdrMsg->GetSize() << ", "
      //           << "TF id: " << lReadoutHdr.timeframeId << ", "
      //           << "#HBF: " << lReadoutHdr.numberOfHBF << ", "
      //           << "EQ: " << lReadoutHdr.linkId;

      // check for new TF marker
      if (lReadoutHdr.timeframeId != lCurrentStfId) {
        assert(lHBFramesToReceive == 0);

        // assert(lReadoutHdr.timeframeId > lCurrentStfId);
        if (lReadoutHdr.timeframeId < lCurrentStfId) {
          LOG(WARN) << "TF ID decreased! (" << lCurrentStfId << ") -> (" << lReadoutHdr.timeframeId << ")";
          // what now?
        }

        if (lCurrentStfId >= 0) {
          // Finished: queue the current STF
          // LOG(INFO) << "Received TF[" << lCurrentStf.Header().mId<< "]::size= " << lCurrentStf.getDataSize();
          mDevice.QueueStf(std::move(lCurrentStf));
        }

        // start a new STF
        lCurrentStfId = lReadoutHdr.timeframeId;
        lCurrentStf = SubTimeFrame(StfBuilderDevice::gStfOutputChanId, lCurrentStfId);
      }

      // handle HBFrames
      assert(lReadoutHdr.numberOfHBF > 0);
      lHBFramesToReceive = lReadoutHdr.numberOfHBF;

      // receive trailing HBFrames
      while (lHBFramesToReceive > 0) {

        FairMQMessagePtr lHBFrameMsg = lInputChan.NewMessage();
        if ((ret = lInputChan.Receive(lHBFrameMsg)) < 0)
          throw std::runtime_error("HBFrame receive failed (err = " + std::to_string(ret) + ")");

        lHBFrameMsgs.emplace_back(std::move(lHBFrameMsg));
        lHBFramesToReceive--;
      }

      // add HBFrames to the current STF
      lCurrentStf.addHBFrames(lReadoutHdr, std::move(lHBFrameMsgs));
      assert(lHBFrameMsgs.size() == 0);

    } catch (std::runtime_error& e) {
      LOG(ERROR) << "Receive failed. Stopping input thread[" << pInputChannelIdx << "]...";
      if (mDevice.CheckCurrentState(StfBuilderDevice::RUNNING))
        mDevice.ChangeState(StfBuilderDevice::END);
      return;
    }
  }

  LOG(INFO) << "Exiting input thread[" << pInputChannelIdx << "]...";
}


}
}


