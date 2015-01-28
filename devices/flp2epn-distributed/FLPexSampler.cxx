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
  : fEventSize(10000)
  , fEventRate(1)
  , fEventCounter(0)
{
}

FLPexSampler::~FLPexSampler()
{
}

void FLPexSampler::Run()
{
  LOG(INFO) << ">>>>>>> Run <<<<<<<";
  boost::this_thread::sleep(boost::posix_time::milliseconds(10000));

  boost::thread rateLogger(boost::bind(&FairMQDevice::LogSocketRates, this));
  boost::thread resetEventCounter(boost::bind(&FLPexSampler::ResetEventCounter, this));

  unsigned long eventId = 1;
  // int numEvents = 1024;

  void* buffer = operator new[](fEventSize);
  FairMQMessage* baseMsg = fTransportFactory->CreateMessage(buffer, fEventSize);

  while (fState == RUNNING) {
    // if (eventId <= numEvents) {
      FairMQMessage* idPart = fTransportFactory->CreateMessage(sizeof(unsigned long));
      memcpy(idPart->GetData(), &eventId, sizeof(unsigned long));

      fPayloadOutputs->at(0)->Send(idPart, "snd-more");
      ++eventId;

      FairMQMessage* dataPart = fTransportFactory->CreateMessage();
      dataPart->Copy(baseMsg);

      if (fPayloadOutputs->at(0)->Send(dataPart, "no-block") == 0) {
        LOG(ERROR) << "Could not send message with event #" << eventId << " without blocking";
      }

      // LOG(INFO) << "Sent event #" << eventId;
      // if (eventId == numEvents) {
      //   LOG(INFO) << "Sent " << numEvents << " events";
      // }

      // boost::this_thread::sleep(boost::posix_time::milliseconds(200));

      --fEventCounter;

      while (fEventCounter == 0) {
        boost::this_thread::sleep(boost::posix_time::milliseconds(1));
      }

      delete idPart;
      delete dataPart;
    // } else {
    //   boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
    // }
  }

  delete baseMsg;

  try {
    rateLogger.interrupt();
    rateLogger.join();
    resetEventCounter.interrupt();
    resetEventCounter.join();
  } catch(boost::thread_resource_error& e) {
    LOG(ERROR) << e.what();
  }

  FairMQDevice::Shutdown();

  // notify parent thread about end of processing.
  boost::lock_guard<boost::mutex> lock(fRunningMutex);
  fRunningFinished = true;
  fRunningCondition.notify_one();
}

void FLPexSampler::ResetEventCounter()
{
  while (true) {
    try {
      fEventCounter = fEventRate / 100;
      boost::this_thread::sleep(boost::posix_time::milliseconds(10));
    } catch (boost::thread_interrupted&) {
      break;
    }
  }
}

void FLPexSampler::SetProperty(const int key, const string& value, const int slot /*= 0*/)
{
  switch (key) {
    default:
      FairMQDevice::SetProperty(key, value, slot);
      break;
  }
}

string FLPexSampler::GetProperty(const int key, const string& default_ /*= ""*/, const int slot /*= 0*/)
{
  switch (key) {
    default:
      return FairMQDevice::GetProperty(key, default_, slot);
  }
}

void FLPexSampler::SetProperty(const int key, const int value, const int slot /*= 0*/)
{
  switch (key) {
    case EventSize:
      fEventSize = value;
      break;
    case EventRate:
      fEventRate = value;
      break;
    default:
      FairMQDevice::SetProperty(key, value, slot);
      break;
  }
}

int FLPexSampler::GetProperty(const int key, const int default_ /*= 0*/, const int slot /*= 0*/)
{
  switch (key) {
    case EventSize:
      return fEventSize;
    case EventRate:
      return fEventRate;
    default:
      return FairMQDevice::GetProperty(key, default_, slot);
  }
}
