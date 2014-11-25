/**
 * FLPex.cxx
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <vector>

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "FairMQLogger.h"
#include "FairMQPoller.h"

#include "FLPex.h"

using namespace std;

using namespace AliceO2::Devices;

FLPex::FLPex()
  : fSendOffset(0)
  , fEventSize(10000)
  , fEventRate(1)
  , fEventCounter(0)
{
}

FLPex::~FLPex()
{
}

void FLPex::Init()
{
  FairMQDevice::Init();

  fScheduler.setVerbosity(0);

  int err = fScheduler.initConnexion("127.0.0.1:2181");
  if (err > 0) {
    LOG(ERROR) << "initConnexion() failed: error " << err;
  }

  err = fScheduler.registerFlp(stoi(fId));
  if (err) {
    LOG(ERROR) << "registerFlp() failed: error " << err;
  }
}

void FLPex::Run()
{
  LOG(INFO) << ">>>>>>> Run <<<<<<<";

  boost::thread rateLogger(boost::bind(&FairMQDevice::LogSocketRates, this));
  boost::thread resetEventCounter(boost::bind(&FLPex::ResetEventCounter, this));

  FairMQPoller* poller = fTransportFactory->CreatePoller(*fPayloadInputs);

  unsigned long timeframeId = 0;

  // base buffer, to be copied from for every payload
  void* buffer = operator new[](fEventSize);
  FairMQMessage* baseMsg = fTransportFactory->CreateMessage(buffer, fEventSize);

  bool started = false;
  uint16_t direction = 0;
  int counter = 0;

  // wait for the start signal before starting any work.
  LOG(INFO) << "waiting for the start signal...";

  while (fState == RUNNING) {
    poller->Poll(0);

    // input 0 - commands
    if (poller->CheckInput(0)) {
      FairMQMessage* commandMsg = fTransportFactory->CreateMessage();

      if (fPayloadInputs->at(0)->Receive(commandMsg) > 0) {
        //... handle command ...
      }

      delete commandMsg;
    }

    // input 1 - start signal
    if (poller->CheckInput(1)) {
      FairMQMessage* startSignal = fTransportFactory->CreateMessage();
      fPayloadInputs->at(1)->Receive(startSignal);
      delete startSignal;
      LOG(INFO) << "starting!";
      started = true;
    }

    if (started) {
      // for which EPN is the message?

      // initialize and store id msg part in the buffer.
      FairMQMessage* idPart = fTransportFactory->CreateMessage(sizeof(unsigned long));
      memcpy(idPart->GetData(), &timeframeId, sizeof(unsigned long));
      fIdBuffer.push(idPart);

      // initialize and store data msg part in the buffer.
      FairMQMessage* dataPart = fTransportFactory->CreateMessage();
      dataPart->Copy(baseMsg);
      fDataBuffer.push(dataPart);

      if (counter == fSendOffset) {
        unsigned long currentTimeframeId = *(reinterpret_cast<unsigned long*>(fIdBuffer.front()->GetData()));
        int err = fScheduler.getEpnIdFromTimeframeId(currentTimeframeId, direction);
        // LOG(INFO) << "Sending timeframe " << currentTimeframeId << " to EPN#" << direction << "...";

        switch (err) {
          case (EpnScheduler::OK):
            fPayloadOutputs->at(direction)->Send(fIdBuffer.front(), "snd-more");
            if (fPayloadOutputs->at(direction)->Send(fDataBuffer.front(), "no-block") == 0) {
              LOG(ERROR) << "Could not send message with timeframe #" << currentTimeframeId << " without blocking";
            }
            fIdBuffer.pop();
            fDataBuffer.pop();
            break;
          case (EpnScheduler::AHEAD):
            LOG(WARN) << "droping timeframe for EPN #" << direction << ", because we are late on the schedule";
            fIdBuffer.pop();
            fDataBuffer.pop();
            break;
          default:
            LOG(ERROR) << "getEpnIdFromTimeframeId() failed: error " << err;
            break;
        }
      } else if (counter < fSendOffset) {
        LOG(INFO) << "Buffering timeframe...";
        ++counter;
      } else {
        LOG(ERROR) << "Counter larger than offset, something went wrong...";
      }

      ++timeframeId;

      --fEventCounter;

      while (fEventCounter == 0) {
        boost::this_thread::sleep(boost::posix_time::milliseconds(1));
      }

    }

  } // while (fState == RUNNING)

  delete baseMsg;

  resetEventCounter.interrupt();
  resetEventCounter.join();
  rateLogger.interrupt();
  rateLogger.join();

  FairMQDevice::Shutdown();

  // notify parent thread about end of processing.
  boost::lock_guard<boost::mutex> lock(fRunningMutex);
  fRunningFinished = true;
  fRunningCondition.notify_one();
}

void FLPex::ResetEventCounter()
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

void FLPex::SetProperty(const int key, const string& value, const int slot/*= 0*/)
{
  switch (key) {
    default:
      FairMQDevice::SetProperty(key, value, slot);
      break;
  }
}

string FLPex::GetProperty(const int key, const string& default_/*= ""*/, const int slot/*= 0*/)
{
  switch (key) {
    default:
      return FairMQDevice::GetProperty(key, default_, slot);
  }
}

void FLPex::SetProperty(const int key, const int value, const int slot/*= 0*/)
{
  switch (key) {
    case SendOffset:
      fSendOffset = value;
      break;
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

int FLPex::GetProperty(const int key, const int default_/*= 0*/, const int slot/*= 0*/)
{
  switch (key) {
    case SendOffset:
      return fSendOffset;
    case EventSize:
      return fEventSize;
    case EventRate:
      return fEventRate;
    default:
      return FairMQDevice::GetProperty(key, default_, slot);
  }
}
