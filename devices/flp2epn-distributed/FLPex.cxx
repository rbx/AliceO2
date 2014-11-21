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
using boost::posix_time::ptime;

using namespace AliceO2::Devices;

FLPex::FLPex()
  : fHeartbeatTimeoutInMs(20000)
  , fSendOffset(0)
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

  ptime nullTime;

  for (int i = 0; i < fNumOutputs; ++i) {
    fOutputHeartbeat.push_back(nullTime);
  }
}

bool FLPex::updateIPHeartbeat(string reply)
{
  for (int i = 0; i < fNumOutputs; ++i) {
    if (GetProperty(OutputAddress, "", i) == reply) {
      ptime currentHeartbeat = boost::posix_time::microsec_clock::local_time();
      ptime storedHeartbeat = GetProperty(OutputHeartbeat, storedHeartbeat, i);

      if (to_simple_string(storedHeartbeat) != "not-a-date-time") {
        // LOG(INFO) << "EPN " << i << " (" << reply << ")" << " last seen "
        //           << (currentHeartbeat - storedHeartbeat).total_milliseconds() << " ms ago.";
      }
      else {
        LOG(INFO) << "IP has no heartbeat associated. Adding heartbeat: " << currentHeartbeat;
      }

      SetProperty(OutputHeartbeat, currentHeartbeat, i);

      return true;
    }
  }
  LOG(ERROR) << "IP " << reply << " unknown, not provided at execution time";

  return false;
}

void FLPex::Run()
{
  LOG(INFO) << ">>>>>>> Run <<<<<<<";

  boost::thread rateLogger(boost::bind(&FairMQDevice::LogSocketRates, this));
  boost::thread resetEventCounter(boost::bind(&FLPex::ResetEventCounter, this));

  FairMQPoller* poller = fTransportFactory->CreatePoller(*fPayloadInputs);

  unsigned long eventId = 0;

  // base buffer, to be copied from for every payload
  void* buffer = operator new[](fEventSize);
  FairMQMessage* baseMsg = fTransportFactory->CreateMessage(buffer, fEventSize);

  bool started = false;
  int direction = 0;
  int counter = 0;
  ptime currentHeartbeat;
  ptime storedHeartbeat;

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

    // input 1 - heartbeats
    if (poller->CheckInput(1)) {
      FairMQMessage* heartbeatMsg = fTransportFactory->CreateMessage();

      if (fPayloadInputs->at(1)->Receive(heartbeatMsg) > 0) {
        string reply = string(static_cast<char*>(heartbeatMsg->GetData()), heartbeatMsg->GetSize());
        updateIPHeartbeat(reply);
      }

      delete heartbeatMsg;
    }

    // input 1 - heartbeats
    if (poller->CheckInput(2)) {
      FairMQMessage* startSignal = fTransportFactory->CreateMessage();
      fPayloadInputs->at(2)->Receive(startSignal);
      delete startSignal;
      LOG(INFO) << "starting!";
      started = true;
    }

    if (started) {
      // for which EPN is the message?
      direction = eventId % fNumOutputs;

      // initialize and store id msg part in the buffer.
      FairMQMessage* idPart = fTransportFactory->CreateMessage(sizeof(unsigned long));
      memcpy(idPart->GetData(), &eventId, sizeof(unsigned long));
      fIdBuffer.push(idPart);

      // initialize and store data msg part in the buffer.
      FairMQMessage* dataPart = fTransportFactory->CreateMessage();
      dataPart->Copy(baseMsg);
      fDataBuffer.push(dataPart);

      if (counter == fSendOffset) {
        // LOG(INFO) << "Sending event " << eventId << " to EPN#" << direction << "...";

        currentHeartbeat = boost::posix_time::microsec_clock::local_time();
        storedHeartbeat = GetProperty(OutputHeartbeat, storedHeartbeat, direction);

        // if the heartbeat from the corresponding EPN is within timeout period, send the data.
        if (to_simple_string(storedHeartbeat) != "not-a-date-time" ||
            (currentHeartbeat - storedHeartbeat).total_milliseconds() < fHeartbeatTimeoutInMs) {
          fPayloadOutputs->at(direction)->Send(fIdBuffer.front(), "snd-more");
          if (fPayloadOutputs->at(direction)->Send(fDataBuffer.front(), "no-block") == 0) {
            LOG(ERROR) << "Could not send message with event #" << eventId << " without blocking";
          }
          fIdBuffer.pop();
          fDataBuffer.pop();
        } else { // if the heartbeat is too old, discard the data.
          LOG(WARN) << "Heartbeat too old for EPN#" << direction << ", discarding message.";
          LOG(WARN) << (currentHeartbeat - storedHeartbeat).total_milliseconds();
          fIdBuffer.pop();
          fDataBuffer.pop();
        }
      } else if (counter < fSendOffset) {
        LOG(INFO) << "Buffering event...";
        ++counter;
      } else {
        LOG(ERROR) << "Counter larger than offset, something went wrong...";
      }

      ++eventId;

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
    case HeartbeatTimeoutInMs:
      fHeartbeatTimeoutInMs = value;
      break;
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
    case HeartbeatTimeoutInMs:
      return fHeartbeatTimeoutInMs;
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

// Method for setting properties represented as a heartbeat.
void FLPex::SetProperty(const int key, const ptime value, const int slot /*= 0*/)
{
  switch (key) {
    case OutputHeartbeat:
      fOutputHeartbeat.erase(fOutputHeartbeat.begin() + slot);
      fOutputHeartbeat.insert(fOutputHeartbeat.begin() + slot, value);
      break;
  }
}

// Method for getting properties represented as a heartbeat.
ptime FLPex::GetProperty(const int key, const ptime default_, const int slot /*= 0*/)
{
  switch (key) {
    case OutputHeartbeat:
      return fOutputHeartbeat.at(slot);
  }
}
