// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.


#include "TimeFrameBuilder/TimeFrameBuilderInput.h"
#include "TimeFrameBuilder/TimeFrameBuilderDevice.h"

#include "Common/SubTimeFrameDataModel.h"
#include "Common/SubTimeFrameVisitors.h"

#include <O2Device/O2Device.h>
#include <FairMQDevice.h>
#include <FairMQStateMachine.h>
#include <FairMQLogger.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <chrono>

namespace o2 {
namespace DataDistribution {

void TfBuilderInput::Start(unsigned int pNumFlp)
{
  if (!mDevice.CheckCurrentState(TfBuilderDevice::RUNNING)) {
    LOG(WARN) << "Not creating interface threads. StfBuilder is not running.";
    return;
  }

  assert(mInputThreads.size() == 0);

  // start receiver threads
  for (auto tid = 0; tid < pNumFlp; tid++) // tid matches input channel index
    mInputThreads.emplace_back(std::thread(&TfBuilderInput::DataHandlerThread, this, tid));

  std::lock_guard<std::mutex> lQueueLock(mStfMergerQueueLock);
  mStfMergeQueue.clear();

  // start the merger thread
  mStfMergerThread = std::thread(&TfBuilderInput::StfMergerThread, this);
}

void TfBuilderInput::Stop()
{
  assert(!mDevice.CheckCurrentState(TfBuilderDevice::RUNNING));

  // Wait for input threads to stop
  for (auto &lIdThread : mInputThreads)
    lIdThread.join();

  // Make sure the merger stopped
  {
    std::lock_guard<std::mutex> lQueueLock(mStfMergerQueueLock);
    mStfMergeQueue.clear();
  }
  mStfMergerThread.join();

  LOG(INFO) << "TfBuilderInput: Teardown complete...";
}

/// Receiving thread
void TfBuilderInput::DataHandlerThread(const std::uint32_t pFlpIndex)
{
  // Reference to the input channel
  auto &lInputChan = mDevice.GetChannel(mDevice.getInputChannelName(), pFlpIndex);

  // receive a STF
#if STF_SERIALIZATION == 1
  InterleavedHdrDataDeserializer lStfReceiver(lInputChan);
#elif STF_SERIALIZATION == 2
  HdrDataDeserializer lStfReceiver(lInputChan);
#else
#error "Unknown STF_SERIALIZATION type"
#endif

  while (mDevice.CheckCurrentState(TfBuilderDevice::RUNNING)) {

    SubTimeFrame lStf;

    if (!lStfReceiver.deserialize(lStf)) {
      if (mDevice.CheckCurrentState(TfBuilderDevice::RUNNING))
        LOG(WARN) << "InputThread[" << pFlpIndex << "]: Receive failed";
      else
        LOG(INFO) << "InputThread[" << pFlpIndex << "](NOT RUNNING): Receive failed";
      break;
    }

    const TimeFrameIdType lTfId = lStf.Header().mId;

    // Push the STF into the merger queue
    std::unique_lock<std::mutex> lQueueLock(mStfMergerQueueLock);
    mStfMergeQueue.emplace(std::make_pair(lTfId, std::move(lStf)));

    // Notify the Merger if enough inputs are collected
    // NOW:  Merge STFs if exactly |FLP| chunks have been received
    //       or a next TF started arriving (STFs from previous delayed or not
    //       available)
    // TODO: Find out exactly how many STFs is arriving.
    if (mStfMergeQueue.size() >= mDevice.getFlpNodeCount())
      mStfMergerCondition.notify_one();
  }

  LOG(INFO) << "Exiting input thread[" << pFlpIndex << "]...";
}


/// STF->TF Merger thread
void TfBuilderInput::StfMergerThread()
{
  using namespace std::chrono_literals;

  while (mDevice.CheckCurrentState(TfBuilderDevice::RUNNING)) {

    std::unique_lock<std::mutex> lQueueLock(mStfMergerQueueLock);
    if (std::cv_status::timeout == mStfMergerCondition.wait_for(lQueueLock, 500ms))
      continue;

    // check the merge queue for partial TFs first
    auto lStfBegin = mStfMergeQueue.begin();
    // we should not be working on an empty queue

    if (!mStfMergeQueue.empty())
      assert(lStfBegin != std::end(mStfMergeQueue));

    const SubTimeFrameIdType lTfId = lStfBegin->first;
    auto lStfRange = mStfMergeQueue.equal_range(lTfId);

    // Case 1: the queue contains STF with two different IDs
    // Case 2: a full TF can be merged
    if (lStfRange.second != std::end(mStfMergeQueue) ||
        mStfMergeQueue.count(lTfId) == mDevice.getFlpNodeCount()
      ) {

      auto lStfCount = 1UL; // start from the first element
      SubTimeFrame lTf = std::move(lStfRange.first->second);

      for (auto lStfIter = std::next(lStfRange.first); lStfIter != lStfRange.second; ++lStfIter) {
        // Add them all up
        lTf += std::move(lStfIter->second);
        lStfCount++;
      }

      if (lStfCount < mDevice.getFlpNodeCount())
        LOG(WARN) << "STF MergerThread: merging incomplete TF[" << lStfCount << "]: contains "
                  << lStfCount << " instead of " << mDevice.getFlpNodeCount() << " SubTimeFrames";

      // remove consumed STFs from the merge queue
      mStfMergeQueue.erase(lStfRange.first, lStfRange.second);

      // Queue out the TF for consumption
      mDevice.QueueTf(std::move(lTf));
    }

  }

  LOG(INFO) << "Exiting STF merger thread...";
}

}
} /* namespace o2::DataDistribution */
