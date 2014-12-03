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
  , fSchedulerAddress("")
{
}

FLPex::~FLPex()
{
}

void FLPex::Init()
{
  FairMQDevice::Init();

  fScheduler.setVerbosity(0);

  LOG(WARN) << fSchedulerAddress.c_str();

  int err = fScheduler.initConnexion(fSchedulerAddress.c_str());
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

  FairMQPoller* poller = fTransportFactory->CreatePoller(*fPayloadInputs);

  uint64_t timeframeId = 0;

  // base buffer, to be copied from for every payload
  void* buffer = operator new[](fEventSize);
  FairMQMessage* baseMsg = fTransportFactory->CreateMessage(buffer, fEventSize);

  uint16_t direction = 0;
  int counter = 0;



  while (fState == RUNNING) {
    poller->Poll(-1);

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
    }

  } // while (fState == RUNNING)

  delete baseMsg;

  rateLogger.interrupt();
  rateLogger.join();

  FairMQDevice::Shutdown();

  // notify parent thread about end of processing.
  boost::lock_guard<boost::mutex> lock(fRunningMutex);
  fRunningFinished = true;
  fRunningCondition.notify_one();
}

void FLPex::SetProperty(const int key, const string& value, const int slot/*= 0*/)
{
  switch (key) {
    case SchedulerAddress:
      fSchedulerAddress = value;
      break;
    default:
      FairMQDevice::SetProperty(key, value, slot);
      break;
  }
}

string FLPex::GetProperty(const int key, const string& default_/*= ""*/, const int slot/*= 0*/)
{
  switch (key) {
    case SchedulerAddress:
      return fSchedulerAddress;
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
    default:
      return FairMQDevice::GetProperty(key, default_, slot);
  }
}
