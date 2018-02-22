// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "TimeFrameBuilder/TimeFrameBuilderDevice.h"
#include "Common/SubTimeFrameDataModel.h"
#include "Common/SubTimeFrameVisitors.h"

#include <options/FairMQProgOptions.h>
#include <FairMQLogger.h>

#include <TH1.h>

#include <chrono>
#include <thread>

namespace o2 {
namespace DataDistribution {

TfBuilderDevice::TfBuilderDevice()
: O2Device{},
  mFlpInputHandler(*this),
  mTfRootApp("TfBuilderApp", nullptr, nullptr),
  mTfBuilderCanvas("cnv", "TF Builder", 1000, 500),
  mTfSizeSamples(10000),
  mTfFreqSamples(10000)
{
  mTfBuilderCanvas.Divide(2, 1);
}

TfBuilderDevice::~TfBuilderDevice()
{
}

void TfBuilderDevice::InitTask()
{
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mFlpNodeCount = GetConfig()->GetValue<std::uint32_t>(OptionKeyFlpNodeCount);
}

void TfBuilderDevice::PreRun()
{
  // Start output handlers
  mFlpInputHandler.Start(mFlpNodeCount);
  // start the gui thread
  mGuiThread = std::thread(&TfBuilderDevice::GuiThread, this);
}

void TfBuilderDevice::PostRun()
{
  LOG(INFO) << "PostRun() start... ";
  // stop the main FSM
  mTfQueue.stop();
  // stop output handlers
  mFlpInputHandler.Stop();
  //wait for the gui thread
  mGuiThread.join();

  LOG(INFO) << "PostRun() done... ";
}

bool TfBuilderDevice::ConditionalRun()
{
  static auto lFreqStartTime = std::chrono::high_resolution_clock::now();

  SubTimeFrame lTf;
  if (!mTfQueue.pop(lTf) ) {
    LOG(INFO) << "ConditionalRun(): Exiting... ";
    return false;
  }

  // record frequency and size of TFs
  if (mBuildHistograms) {
      mTfFreqSamples.Fill(
        1.0 / std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - lFreqStartTime).count());

    lFreqStartTime = std::chrono::high_resolution_clock::now();

    // size histogram
    mTfSizeSamples.Fill(lTf.getDataSize());
  }

  // Do something with the TF

  {
    // is there a ratelimited LOG?
    static unsigned long floodgate = 0;
    if (++floodgate % 100 == 1)
      LOG(DEBUG) << "TF[" << lTf.Header().mId << "] size: " << lTf.getDataSize();
  }

  return true;
}



void TfBuilderDevice::GuiThread()
{
  while (CheckCurrentState(RUNNING)) {
    LOG(INFO) << "Updating histograms...";

    TH1F lTfSizeHist("TfSizeH", "Size of TF", 100, 0.0, float(1UL << 30));
    lTfSizeHist.GetXaxis()->SetTitle("Size [B]");
    for (const auto v : mTfSizeSamples)
      lTfSizeHist.Fill(v);

    mTfBuilderCanvas.cd(1);
    lTfSizeHist.Draw();

    TH1F lTfFreqHist("TfFreq", "TimeFrame frequency", 200, 0.0, 100.0);
    lTfFreqHist.GetXaxis()->SetTitle("Frequency [Hz]");
    for (const auto v : mTfFreqSamples)
      lTfFreqHist.Fill(v);

    mTfBuilderCanvas.cd(2);
    lTfFreqHist.Draw();

    mTfBuilderCanvas.Modified();
    mTfBuilderCanvas.Update();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5s);
  }

  LOG(INFO) << "Exiting GUI thread...";
}


}
} /* namespace o2::DataDistribution */
