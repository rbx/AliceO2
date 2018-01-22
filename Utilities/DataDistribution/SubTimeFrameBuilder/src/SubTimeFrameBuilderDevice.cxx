// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "SubTimeFrameBuilder/SubTimeFrameBuilderDevice.h"

#include "Common/SubTimeFrameUtils.h"
#include "Common/SubTimeFrameVisitors.h"
#include "Common/ReadoutDataModel.h"
#include "Common/SubTimeFrameDataModel.h"
#include "Common/Utilities.h"

#include <TH1.h>

#include <options/FairMQProgOptions.h>
#include <FairMQLogger.h>

#include <chrono>
#include <thread>
#include <queue>

namespace o2 {
namespace DataDistribution {

constexpr int StfBuilderDevice::gStfOutputChanId;

StfBuilderDevice::StfBuilderDevice()
  : O2Device{},
    mReadoutInterface(*this),
    mStfRootApp("StfBuilderApp", nullptr, nullptr),
    mStfBuilderCanvas("cnv", "STF Builder", 1500, 500),
    mStfSizeSamples(10000),
    mStfFreqSamples(10000),
    mStfDataTimeSamples(10000)
{
  mStfBuilderCanvas.Divide(3, 1);
}

StfBuilderDevice::~StfBuilderDevice()
{
  mOutputThread.join();
}

void StfBuilderDevice::InitTask()
{
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mOutputChannelName = GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
  mCruCount = GetConfig()->GetValue<std::uint64_t>(OptionKeyCruCount);

  ChannelAllocator::get().addChannel(gStfOutputChanId, this, mOutputChannelName, 0);

  if (mCruCount < 1 || mCruCount > 32) {
    LOG(ERROR) << "CRU count parameter is not configured properly: " << mCruCount;
    exit(-1);
  }
}

void StfBuilderDevice::PreRun()
{
  // start output thread
  mOutputThread = std::thread(&StfBuilderDevice::StfOutputThread, this);

  // start one thread per readout process
  mReadoutInterface.Start(mCruCount);

  // gui thread
  mGuiThread = std::thread(&StfBuilderDevice::GuiThread, this);
}

void StfBuilderDevice::PostRun()
{
  // stop readout interface threads
  mReadoutInterface.Stop();

  // stop the optput thread
  mOutputThread.join();

  mGuiThread.join();
}

bool StfBuilderDevice::ConditionalRun()
{
  // ugh: sleep for a while
  usleep(500000);
  return true;
}

void StfBuilderDevice::StfOutputThread()
{
  while (CheckCurrentState(RUNNING)) {
    SubTimeFrame lStf;

    const auto lFreqStartTime = std::chrono::high_resolution_clock::now();

    if (!mStfQueue.pop(lStf)) {
      LOG(WARN) << "Stopping StfOutputThread...";
      return;
    }

    if (mBuildHistograms)
      mStfFreqSamples.Fill(
        1.0 / std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - lFreqStartTime).count());


    const auto lStartTime = std::chrono::high_resolution_clock::now();

#ifdef STF_FILTER_EXAMPLE
    // EXAMPLE:
    // split one SubTimeFrame into per detector SubTimeFrames (this is unlikely situation for a STF
    // but still... The TimeFrame structure should be similar)
    static const DataIdentifier cTPCDataIdentifier(gDataDescriptionAny, gDataOriginTPC);
    static const DataIdentifier cITSDataIdentifier(gDataDescriptionAny, gDataOriginITS);

    DataIdentifierSplitter lStfDetectorSplitter;
    SubTimeFrame lStfTPC = lStfDetectorSplitter.split(lStf, cTPCDataIdentifier, gStfOutputChanId);
    SubTimeFrame lStfITS = lStfDetectorSplitter.split(lStf, cITSDataIdentifier, gStfOutputChanId);

    if (mBuildHistograms) {
      mStfSizeSamples.Fill(lStfTPC.getDataSize());
      mStfSizeSamples.Fill(lStfITS.getDataSize());
    }

#if STF_SERIALIZATION == 1
    InterleavedHdrDataSerializer lStfSerializer;
    lStfSerializer.serialize(lStfTPC, *this, mOutputChannelName, 0);
    lStfSerializer.serialize(lStfITS, *this, mOutputChannelName, 0);
#elif STF_SERIALIZATION == 2
    HdrDataSerializer lStfSerializer;
    lStfSerializer.serialize(lStfTPC, *this, mOutputChannelName, 0);
    lStfSerializer.serialize(lStfITS, *this, mOutputChannelName, 0);
#else
#error "Unknown STF_SERIALIZATION type"
#endif
#else

    // Send the STF without filtering
#if STF_SERIALIZATION == 1
    InterleavedHdrDataSerializer lStfSerializer;
#elif STF_SERIALIZATION == 2
    HdrDataSerializer lStfSerializer;
#else
#error "Unknown STF_SERIALIZATION type"
#endif

    if (mBuildHistograms)
      mStfSizeSamples.Fill(lStf.getDataSize());

    lStfSerializer.serialize(lStf, *this, mOutputChannelName, 0);

#endif
    double lTimeMs =
      std::chrono::duration<double, std::micro>(std::chrono::high_resolution_clock::now() - lStartTime).count();
    mStfDataTimeSamples.Fill(lTimeMs / 1000.0);
  }
}


void StfBuilderDevice::GuiThread()
{
  while (CheckCurrentState(RUNNING)) {
    LOG(INFO) << "Updating histograms...";

    TH1F lStfSizeHist("StfSizeH", "Readout data size per STF", 100, 0.0, 400e+6);
    lStfSizeHist.GetXaxis()->SetTitle("Size [B]");
    for (const auto v : mStfSizeSamples)
      lStfSizeHist.Fill(v);

    mStfBuilderCanvas.cd(1);
    lStfSizeHist.Draw();

    TH1F lStfFreqHist("STFFreq", "SubTimeFrame frequency", 200, 0.0, 100.0);
    lStfFreqHist.GetXaxis()->SetTitle("Frequecy [Hz]");
    for (const auto v : mStfFreqSamples)
      lStfFreqHist.Fill(v);

    mStfBuilderCanvas.cd(2);
    lStfFreqHist.Draw();

    TH1F lStfDataTimeHist("StfChanTimeH", "STF on-channel time", 200, 0.0, 30.0);
    lStfDataTimeHist.GetXaxis()->SetTitle("Time [ms]");
    for (const auto v : mStfDataTimeSamples)
      lStfDataTimeHist.Fill(v);

    mStfBuilderCanvas.cd(3);
    lStfDataTimeHist.Draw();

    mStfBuilderCanvas.Modified();
    mStfBuilderCanvas.Update();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5s);
  }
}
}
} /* namespace o2::DataDistribution */
