/**
 * FLPSender.cxx
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <vector>
#include <cstdint> // UINT64_MAX

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "FairMQLogger.h"

#include "FLPSender.h"

using namespace std;
using boost::posix_time::ptime;

using namespace AliceO2::Devices;

struct f2eHeader {
  uint64_t timeFrameId;
  int      flpIndex;
};

FLPSender::FLPSender()
  : fIndex(0)
  , fSendOffset(0)
  , fSendDelay(8)
  , fHeaderBuffer()
  , fDataBuffer()
  , fArrivalTime()
  , fSndMoreFlag(0)
  , fNoBlockFlag(0)
  , fNumEPNs(0)
  , fHeartbeatTimeoutInMs(20000)
  , fHeartbeats()
  , fHeartbeatMutex()
  , fEventSize(10000)
  , fTestMode(0)
{
}

FLPSender::~FLPSender()
{
}

void FLPSender::InitTask()
{
  ptime nullTime;

  fNumEPNs = fChannels.at("data-out").size();

  for (int i = 0; i < fNumEPNs; ++i) {
    fHeartbeats[fChannels.at("data-out").at(i).GetAddress()] = nullTime;
  }
}

void FLPSender::receiveHeartbeats()
{
  FairMQChannel& hbChannel = fChannels.at("heartbeat-in").at(0);

  while (CheckCurrentState(RUNNING)) {
    try {
      unique_ptr<FairMQMessage> hbMsg(fTransportFactory->CreateMessage());

      if (hbChannel.Receive(hbMsg) > 0) {
        string address = string(static_cast<char*>(hbMsg->GetData()), hbMsg->GetSize());

        if (fHeartbeats.find(address) != fHeartbeats.end()) {
            ptime now = boost::posix_time::microsec_clock::local_time();
            ptime storedHeartbeat = fHeartbeats[address];

            if (to_simple_string(storedHeartbeat) != "not-a-date-time") {
              // LOG(INFO) << address << " last seen " << (now - storedHeartbeat).total_milliseconds() << " ms ago.";
            } else {
              LOG(INFO) << "IP " << address << " has no heartbeat associated, adding: " << now;
            }

            boost::unique_lock<boost::shared_mutex> uniqueLock(fHeartbeatMutex);
            fHeartbeats[address] = now;
        } else {
          LOG(ERROR) << "IP " << address << " unknown, not provided at execution time";
        }
      }
    } catch (boost::thread_interrupted&) {
      LOG(INFO) << "FLPSender::receiveHeartbeats() interrupted";
      break;
    }
  }
}

void FLPSender::Run()
{
  boost::thread heartbeatReceiver(boost::bind(&FLPSender::receiveHeartbeats, this));

  // base buffer, to be copied from for every timeframe body
  void* buffer = operator new[](fEventSize);
  unique_ptr<FairMQMessage> baseMsg(fTransportFactory->CreateMessage(buffer, fEventSize));

  fSndMoreFlag = fChannels.at("data-in").at(0).fSocket->SNDMORE;
  fNoBlockFlag = fChannels.at("data-in").at(0).fSocket->NOBLOCK;

  uint64_t timeFrameId = 0;

  // store the channel reference to avoid traversing the map on every loop iteration
  FairMQChannel& dataInChannel = fChannels.at("data-in").at(0);

  while (CheckCurrentState(RUNNING)) {
    // initialize f2e header
    f2eHeader* h = new f2eHeader;

    if (fTestMode > 0) {
      // test-mode: receive and store id part in the buffer.
      unique_ptr<FairMQMessage> idPart(fTransportFactory->CreateMessage());
      if (dataInChannel.Receive(idPart) > 0) {
        h->timeFrameId = *(reinterpret_cast<uint64_t*>(idPart->GetData()));
        h->flpIndex = fIndex;
      } else {
        // if nothing was received, try again
        delete h;
        continue;
      }
    } else {
      // regular mode: use the id generated locally
      h->timeFrameId = timeFrameId;
      h->flpIndex = fIndex;

      if (++timeFrameId == UINT64_MAX - 1) {
        timeFrameId = 0;
      }
    }

    unique_ptr<FairMQMessage> headerPart(fTransportFactory->CreateMessage(h, sizeof(f2eHeader)));
    unique_ptr<FairMQMessage> dataPart(fTransportFactory->CreateMessage());

    // save the arrival time of the message.
    fArrivalTime.push(boost::posix_time::microsec_clock::local_time());

    if (fTestMode > 0) {
      // test-mode: initialize and store data part in the buffer.
      dataPart->Copy(baseMsg);
      fHeaderBuffer.push(move(headerPart));
      fDataBuffer.push(move(dataPart));
    } else {
      // regular mode: receive data part from input
      if (dataInChannel.Receive(dataPart) > 0) {
        fHeaderBuffer.push(move(headerPart));
        fDataBuffer.push(move(dataPart));
      } else {
        // if nothing was received, try again
        continue;
      }
    }

    // LOG(INFO) << fDataBuffer.size();

    // if offset is 0 - send data out without staggering.
    if (fSendOffset == 0 && fDataBuffer.size() > 0) {
      sendFrontData();
    } else if (fDataBuffer.size() > 0) {
      // size_t dataSize = fDataBuffer.front()->GetSize();
      ptime now = boost::posix_time::microsec_clock::local_time();
      if ((now - fArrivalTime.front()).total_milliseconds() >= (fSendDelay * fSendOffset)) {
        sendFrontData();
      } else {
        // LOG(INFO) << "buffering...";
      }
    }
  }

  heartbeatReceiver.interrupt();
  heartbeatReceiver.join();
}

inline void FLPSender::sendFrontData()
{
  f2eHeader h = *(reinterpret_cast<f2eHeader*>(fHeaderBuffer.front()->GetData()));
  uint64_t currentTimeframeId = h.timeFrameId;

  // for which EPN is the message?
  int direction = currentTimeframeId % fNumEPNs;
  // LOG(INFO) << "Sending event " << currentTimeframeId << " to EPN#" << direction << "...";

  ptime currentTime = boost::posix_time::microsec_clock::local_time();
  ptime storedHeartbeat;
  {
    boost::shared_lock<boost::shared_mutex> lock(fHeartbeatMutex);
    storedHeartbeat = fHeartbeats[fChannels.at("data-out").at(direction).GetAddress()];
  }

  // if the heartbeat is too old, discard the data.
  if (to_simple_string(storedHeartbeat) == "not-a-date-time" ||
      (currentTime - storedHeartbeat).total_milliseconds() > fHeartbeatTimeoutInMs) {
    LOG(WARN) << "Heartbeat too old for EPN#" << direction << ", discarding message.";
    fHeaderBuffer.pop();
    fArrivalTime.pop();
    fDataBuffer.pop();
  } else { // if the heartbeat from the corresponding EPN is within timeout period, send the data.
    if (fChannels.at("data-out").at(direction).SendPart(fHeaderBuffer.front()) == 0) {
      LOG(ERROR) << "Could not queue ID part of event #" << currentTimeframeId << " without blocking";
    }
    if (fChannels.at("data-out").at(direction).SendAsync(fDataBuffer.front()) == 0) {
      LOG(ERROR) << "Could not send message with event #" << currentTimeframeId << " without blocking";
    }
    fHeaderBuffer.pop();
    fArrivalTime.pop();
    fDataBuffer.pop();
  }
}

void FLPSender::SetProperty(const int key, const string& value)
{
  switch (key) {
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

string FLPSender::GetProperty(const int key, const string& default_/*= ""*/)
{
  switch (key) {
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}

void FLPSender::SetProperty(const int key, const int value)
{
  switch (key) {
    case HeartbeatTimeoutInMs:
      fHeartbeatTimeoutInMs = value;
      break;
    case Index:
      fIndex = value;
      break;
    case TestMode:
      fTestMode = value;
      break;
    case SendOffset:
      fSendOffset = value;
      break;
    case SendDelay:
      fSendDelay = value;
      break;
    case EventSize:
      fEventSize = value;
      break;
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

int FLPSender::GetProperty(const int key, const int default_/*= 0*/)
{
  switch (key) {
    case HeartbeatTimeoutInMs:
      return fHeartbeatTimeoutInMs;
    case Index:
      return fIndex;
    case TestMode:
      return fTestMode;
    case SendOffset:
      return fSendOffset;
    case SendDelay:
      return fSendDelay;
    case EventSize:
      return fEventSize;
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}
