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
    mStfBuilderCanvas(nullptr),
    mStfSizeSamples(10000),
    mStfFreqSamples(10000),
    mStfDataTimeSamples(10000)
{}

StfBuilderDevice::~StfBuilderDevice()
{
}

void StfBuilderDevice::InitTask()
{
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mOutputChannelName = GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
  mCruCount = GetConfig()->GetValue<std::uint64_t>(OptionKeyCruCount);
  mBuildHistograms = GetConfig()->GetValue<bool>(OptionKeyGui);

  ChannelAllocator::get().addChannel(gStfOutputChanId, GetChannel(mOutputChannelName, 0));

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
  if (mBuildHistograms) {
    mStfBuilderCanvas = std::make_unique<TCanvas>("cnv", "STF Builder", 1500, 500);
    mStfBuilderCanvas->Divide(3, 1);
    mGuiThread = std::thread(&StfBuilderDevice::GuiThread, this);
  }
}

void StfBuilderDevice::PostRun()
{
  // wait for readout interface threads
  mReadoutInterface.Stop();
  // signal and wait for the optput thread
  mStfQueue.stop();
  mOutputThread.join();
  // wait for the gui thread
  if (mBuildHistograms && mGuiThread.joinable()) {
    mGuiThread.join();
  }

  LOG(INFO) << "PostRun() done... ";
}

bool StfBuilderDevice::ConditionalRun()
{
  // ugh: nothing to do here sleep for awhile
  // usleep(500000);
  std::this_thread::sleep_for(std::chrono::duration<uint64_t, std::milli>(500));
  return true;
}

void StfBuilderDevice::StfOutputThread()
{
  auto &lOutputChan = GetChannel(getOutputChannelName(), 0);

  while (CheckCurrentState(RUNNING)) {
    SubTimeFrame lStf;

    const auto lFreqStartTime = std::chrono::high_resolution_clock::now();

    if (!mStfQueue.pop(lStf))
      break;


    if (mBuildHistograms)
      mStfFreqSamples.Fill(
        1.0 / std::chrono::duration<double>(
        std::chrono::high_resolution_clock::now() - lFreqStartTime).count());

    const auto lStartTime = std::chrono::high_resolution_clock::now();

    // Choose serialization method
#if STF_SERIALIZATION == 1
    InterleavedHdrDataSerializer lStfSerializer(lOutputChan);
#elif STF_SERIALIZATION == 2
    HdrDataSerializer lStfSerializer(lOutputChan);
#else
#error "Unknown STF_SERIALIZATION type"
#endif


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

    // Send filtered data as two objects
    try {
      lStfSerializer.serialize(std::move(lStfTPC));
      lStfSerializer.serialize(std::move(lStfITS));
    } catch (std::exception& e) {
      if (CheckCurrentState(RUNNING))
        LOG(ERROR) << "StfOutputThread: exception on send: " << e.what();
      else
        LOG(INFO) << "StfOutputThread(NOT_RUNNING): exception on send: " << e.what();

      break;
    }
#else

    // Send the STF without filtering
    if (mBuildHistograms)
      mStfSizeSamples.Fill(lStf.getDataSize());

    try {
      lStfSerializer.serialize(std::move(lStf));
    } catch (std::exception& e) {
      if (CheckCurrentState(RUNNING))
        LOG(ERROR) << "StfOutputThread: exception on send: " << e.what();
      else
        LOG(INFO) << "StfOutputThread(NOT_RUNNING): exception on send: " << e.what();

      break;
    }

#endif

    if (mBuildHistograms) {
      double lTimeMs = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - lStartTime).count();
      mStfDataTimeSamples.Fill(lTimeMs);
    }
  }

  LOG(INFO) << "Exiting StfOutputThread...";
}


void StfBuilderDevice::GuiThread()
{
  while (CheckCurrentState(RUNNING)) {
    LOG(INFO) << "Updating histograms...";

    TH1F lStfSizeHist("StfSizeH", "Readout data size per STF", 100, 0.0, 400e+6);
    lStfSizeHist.GetXaxis()->SetTitle("Size [B]");
    for (const auto v : mStfSizeSamples)
      lStfSizeHist.Fill(v);

    mStfBuilderCanvas->cd(1);
    lStfSizeHist.Draw();

    TH1F lStfFreqHist("STFFreq", "SubTimeFrame frequency", 200, 0.0, 100.0);
    lStfFreqHist.GetXaxis()->SetTitle("Frequency [Hz]");
    for (const auto v : mStfFreqSamples)
      lStfFreqHist.Fill(v);

    mStfBuilderCanvas->cd(2);
    lStfFreqHist.Draw();

    TH1F lStfDataTimeHist("StfChanTimeH", "STF on-channel time", 200, 0.0, 30.0);
    lStfDataTimeHist.GetXaxis()->SetTitle("Time [ms]");
    for (const auto v : mStfDataTimeSamples)
      lStfDataTimeHist.Fill(v);

    mStfBuilderCanvas->cd(3);
    lStfDataTimeHist.Draw();

    mStfBuilderCanvas->Modified();
    mStfBuilderCanvas->Update();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(5s);
  }
  LOG(INFO) << "Exiting GUI thread...";
}
}
} /* namespace o2::DataDistribution */
