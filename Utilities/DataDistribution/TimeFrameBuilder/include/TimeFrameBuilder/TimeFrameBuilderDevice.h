// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_TF_BUILDER_DEVICE_H_
#define ALICEO2_TF_BUILDER_DEVICE_H_

#include "TimeFrameBuilder/TimeFrameBuilderInput.h"
#include "Common/SubTimeFrameDataModel.h"
#include "Common/ConcurrentQueue.h"
#include "Common/Utilities.h"

#include "O2Device/O2Device.h"

#include <TApplication.h>
#include <TCanvas.h>
#include <TH1.h>

#include <deque>
#include <mutex>
#include <memory>
#include <condition_variable>

namespace o2 {
namespace DataDistribution {

class TfBuilderDevice : public Base::O2Device {
public:
  static constexpr const char* OptionKeyInputChannelName = "input-channel-name";
  static constexpr const char* OptionKeyFlpNodeCount = "flp-count";
  static constexpr const char* OptionKeyGui = "gui";

  /// Default constructor
  TfBuilderDevice();

  /// Default destructor
  ~TfBuilderDevice() override;

  void InitTask() final;

  const std::string& getInputChannelName() const { return mInputChannelName; }
  const std::uint32_t getFlpNodeCount() const { return mFlpNodeCount; }

  void QueueTf(SubTimeFrame &&pStf) { mTfQueue.push(std::move(pStf)); }

protected:
  void PreRun() final;
  void PostRun() final;
  bool ConditionalRun() final;

  void GuiThread();

  /// Configuration
  std::string mInputChannelName;
  std::uint32_t mFlpNodeCount;

  /// Input Interface handler
  TfBuilderInput mFlpInputHandler;

  /// Internal queues
  ConcurrentFifo<SubTimeFrame> mTfQueue;


  /// Root stuff
  bool mBuildHistograms = true;
  TApplication mTfRootApp; // !?
  std::unique_ptr<TCanvas> mTfBuilderCanvas;
  std::thread mGuiThread;

  RunningSamples<uint64_t> mTfSizeSamples;
  RunningSamples<float> mTfFreqSamples;
};

}
} /* namespace o2::DataDistribution */

#endif /* ALICEO2_TF_BUILDER_DEVICE_H_ */
