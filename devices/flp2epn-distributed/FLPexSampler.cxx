/**
 * FLPexSampler.cpp
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko
 */

#include <vector>

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "FLPexSampler.h"
#include "FairMQLogger.h"

using namespace std;

using namespace AliceO2::Devices;

FLPexSampler::FLPexSampler()
{
}

FLPexSampler::~FLPexSampler()
{
}

void FLPexSampler::Run()
{
  LOG(INFO) << ">>>>>>> Run <<<<<<<";

  int sent = 0;
  int signalValue = 1;

  while (fState == RUNNING) {
    boost::this_thread::sleep(boost::posix_time::milliseconds(10000));

    if (sent == 0) {
      LOG(INFO) << "sending start signal!";
      FairMQMessage* startSignal = fTransportFactory->CreateMessage(signalValue);
      sent = fPayloadOutputs->at(0)->Send(startSignal);
    }
  }

  FairMQDevice::Shutdown();

  // notify parent thread about end of processing.
  boost::lock_guard<boost::mutex> lock(fRunningMutex);
  fRunningFinished = true;
  fRunningCondition.notify_one();
}
