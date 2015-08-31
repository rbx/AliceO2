/**
 * EPNReceiver.cxx
 *
 * @since 2013-01-09
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <fstream> // writing to file (DEBUG)

#include "EPNReceiver.h"
#include "FairMQLogger.h"

using namespace std;
using namespace AliceO2::Devices;

struct f2eHeader {
  uint64_t timeFrameId;
  int      flpIndex;
};

EPNReceiver::EPNReceiver()
  : fHeartbeatIntervalInMs(3000)
  , fBufferTimeoutInMs(1000)
  , fNumFLPs(0)
  , fTestMode(0)
  , fTimeframeBuffer()
  , fDiscardedSet()
{
}

EPNReceiver::~EPNReceiver()
{
}

void EPNReceiver::PrintBuffer(const unordered_map<uint64_t, TFBuffer> &buffer)
{
  string header = "===== ";

  for (int i = 1; i <= fNumFLPs; ++i) {
    stringstream out;
    out << i % 10;
    header += out.str();
    //i > 9 ? header += " " : header += "  ";
  }
  LOG(INFO) << header;

  for (auto& it : buffer) {
    string stars = "";
    for (int i = 1; i <= (it.second).parts.size(); ++i) {
      stars += "*";
    }
    LOG(INFO) << setw(4) << it.first << ": " << stars;
  }
}

void EPNReceiver::DiscardIncompleteTimeframes()
{
  auto it = fTimeframeBuffer.begin();
  while (it != fTimeframeBuffer.end()) {
    if ((boost::posix_time::microsec_clock::local_time() - (it->second).startTime).total_milliseconds() > fBufferTimeoutInMs) {
      LOG(WARN) << "Timeframe #" << it->first << " incomplete after " << fBufferTimeoutInMs << " milliseconds, discarding";
      fDiscardedSet.insert(it->first);
      for (int i = 0; i < (it->second).parts.size(); ++i) {
        (it->second).parts.at(i).reset();
      }
      it->second.parts.clear();
      fTimeframeBuffer.erase(it++);
      LOG(WARN) << "Number of discarded timeframes: " << fDiscardedSet.size();
    } else {
      // LOG(INFO) << "Timeframe #" << it->first << " within timeout, buffering...";
      ++it;
    }
  }
}

void EPNReceiver::Run()
{
  boost::thread heartbeatSender(boost::bind(&EPNReceiver::sendHeartbeats, this));

  unique_ptr<FairMQPoller> poller(fTransportFactory->CreatePoller(fChannels.at("data-in")));

  int SNDMORE = fChannels.at("data-in").at(0).fSocket->SNDMORE;
  int NOBLOCK = fChannels.at("data-in").at(0).fSocket->NOBLOCK;

  // DEBUG: store receive intervals per FLP
  // vector<vector<int>> rcvIntervals(fNumFLPs, vector<int>());
  // vector<boost::posix_time::ptime> rcvTimestamp(fNumFLPs);
  // end DEBUG

  f2eHeader* h; // holds the header of the currently arrived message.
  uint64_t id = 0; // holds the timeframe id of the currently arrived sub-timeframe.
  int rcvDataSize = 0;

  FairMQChannel& dataInputChannel = fChannels.at("data-in").at(0);
  FairMQChannel& dataOutChannel = fChannels.at("data-out").at(0);
  FairMQChannel& ackOutChannel = fChannels.at("ack-out").at(0);

  while (CheckCurrentState(RUNNING)) {
    poller->Poll(100);

    if (poller->CheckInput(0)) {
      unique_ptr<FairMQMessage> headerPart(fTransportFactory->CreateMessage());

      if (dataInputChannel.Receive(headerPart) > 0) {
        // store the received ID
        h = reinterpret_cast<f2eHeader*>(headerPart.get()->GetData());
        id = h->timeFrameId;
        // LOG(INFO) << "Received sub-time frame #" << id << " from FLP" << h->flpIndex;

        // DEBUG:: store receive intervals per FLP
        // if (fTestMode > 0) {
        //   int flp_id = h->flpIndex;
        //   if (to_simple_string(rcvTimestamp.at(flp_id)) != "not_a_date_time") {
        //     rcvIntervals.at(flp_id).push_back( (boost::posix_time::microsec_clock::local_time() - rcvTimestamp.at(flp_id)).total_microseconds() );
        //     // LOG(WARN) << rcvIntervals.at(flp_id).back();
        //   }
        //   rcvTimestamp.at(flp_id) = boost::posix_time::microsec_clock::local_time();
        // }
        // end DEBUG

        unique_ptr<FairMQMessage> dataPart(fTransportFactory->CreateMessage());
        rcvDataSize = dataInputChannel.Receive(dataPart);

        if (fDiscardedSet.find(id) == fDiscardedSet.end()) {
          // if received ID has not previously been discarded.
          if (fTimeframeBuffer.find(id) == fTimeframeBuffer.end()) {
            // if received ID is not yet in the buffer.
            if (rcvDataSize > 0) {
              // receive data, store it in the buffer, save the receive time.
              fTimeframeBuffer[id].parts.push_back(move(dataPart));
              fTimeframeBuffer[id].startTime = boost::posix_time::microsec_clock::local_time();
            } else {
              LOG(ERROR) << "no data received from input socket";
            }
            // PrintBuffer(fTimeframeBuffer);
          } else {
            // if received ID is already in the buffer
            if (rcvDataSize > 0) {
              fTimeframeBuffer[id].parts.push_back(move(dataPart));
            } else {
              LOG(ERROR) << "no data received from input socket";
            }
            // PrintBuffer(fTimeframeBuffer);
          }
        } else {
          // if received ID has been previously discarded.
          LOG(WARN) << "Received part from an already discarded timeframe with id " << id;
        }

        if (fTimeframeBuffer[id].parts.size() == fNumFLPs) {
          // LOG(INFO) << "Collected all parts for timeframe #" << id;
          // when all parts are collected send all except last one with 'snd-more' flag, and last one without the flag.
          for (int i = 0; i < fNumFLPs - 1; ++i) {
            dataOutChannel.SendPart(fTimeframeBuffer[id].parts.at(i));
          }
          dataOutChannel.Send(fTimeframeBuffer[id].parts.at(fNumFLPs - 1));

          if (fTestMode > 0) {
            // Send an acknowledgement back to the sampler to measure the round trip time
            unique_ptr<FairMQMessage> ack(fTransportFactory->CreateMessage(sizeof(uint64_t)));
            memcpy(ack.get()->GetData(), &id, sizeof(uint64_t));

            if (ackOutChannel.SendAsync(ack) == 0) {
              LOG(ERROR) << "Could not send acknowledgement without blocking";
            }
          }

          // let transport know that the data is no longer needed. transport will clean up after it is sent out.
          for(int i = 0; i < fTimeframeBuffer[id].parts.size(); ++i) {
            fTimeframeBuffer[id].parts.at(i).reset();
          }
          fTimeframeBuffer[id].parts.clear();

          // fTimeframeBuffer[id].endTime = boost::posix_time::microsec_clock::local_time();
          // do something with time here ...
          fTimeframeBuffer.erase(id);
        }

        // LOG(WARN) << "Buffer size: " << fTimeframeBuffer.size();
      }
    }

    // check if any incomplete timeframes in the buffer are older than timeout period, and discard them if they are
    DiscardIncompleteTimeframes();
  }

  // DEBUG: save
  // if (fTestMode > 0) {
  //   string name = to_iso_string(boost::posix_time::microsec_clock::local_time()).substr(0, 20);
  //   for (int x = 0; x < fNumFLPs; ++x) {
  //     ofstream flpRcvTimes(fId + "-" + name + "-flp-" + to_string(x) + ".log");
  //     for (auto it = rcvIntervals.at(x).begin() ; it != rcvIntervals.at(x).end(); ++it) {
  //       flpRcvTimes << *it << endl;
  //     }
  //     flpRcvTimes.close();
  //   }
  // }
  // end DEBUG

  heartbeatSender.interrupt();
  heartbeatSender.join();
}

void EPNReceiver::sendHeartbeats()
{
  string ownAddress = fChannels.at("data-in").at(0).GetAddress();
  size_t ownAddressSize = strlen(ownAddress.c_str());

  while (CheckCurrentState(RUNNING)) {
    try {
      for (int i = 0; i < fNumFLPs; ++i) {
        unique_ptr<FairMQMessage> heartbeatMsg(fTransportFactory->CreateMessage(ownAddressSize));
        memcpy(heartbeatMsg.get()->GetData(), ownAddress.c_str(), ownAddressSize);

        fChannels.at("heartbeat-out").at(i).Send(heartbeatMsg);
      }
      boost::this_thread::sleep(boost::posix_time::milliseconds(fHeartbeatIntervalInMs));
    } catch (boost::thread_interrupted&) {
      LOG(INFO) << "EPNReceiver::sendHeartbeat() interrupted";
      break;
    }
  }
}

void EPNReceiver::SetProperty(const int key, const string& value)
{
  switch (key) {
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

string EPNReceiver::GetProperty(const int key, const string& default_/*= ""*/)
{
  switch (key) {
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}

void EPNReceiver::SetProperty(const int key, const int value)
{
  switch (key) {
    case HeartbeatIntervalInMs:
      fHeartbeatIntervalInMs = value;
      break;
    case BufferTimeoutInMs:
      fBufferTimeoutInMs = value;
      break;
    case NumFLPs:
      fNumFLPs = value;
      break;
    case TestMode:
      fTestMode = value;
      break;
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

int EPNReceiver::GetProperty(const int key, const int default_/*= 0*/)
{
  switch (key) {
    case HeartbeatIntervalInMs:
      return fHeartbeatIntervalInMs;
    case BufferTimeoutInMs:
      return fBufferTimeoutInMs;
    case NumFLPs:
      return fNumFLPs;
    case TestMode:
      return fTestMode;
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}
