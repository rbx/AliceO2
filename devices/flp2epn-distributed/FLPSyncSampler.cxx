/**
 * FLPSyncSampler.cpp
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko
 */

#include <vector>
#include <fstream>
#include <cstdint> // UINT64_MAX

#include <boost/thread.hpp>
#include <boost/bind.hpp>

#include "FairMQLogger.h"
#include "FLPSyncSampler.h"

using namespace std;
using boost::posix_time::ptime;

using namespace AliceO2::Devices;

FLPSyncSampler::FLPSyncSampler()
  : fEventRate(1)
  , fEventCounter(0)
{
}

FLPSyncSampler::~FLPSyncSampler()
{
}

void FLPSyncSampler::InitTask()
{
  LOG(INFO) << "Waiting 10 seconds...";
  boost::this_thread::sleep(boost::posix_time::milliseconds(10000));
  LOG(INFO) << "Done!";
}

void FLPSyncSampler::Run()
{
  boost::thread resetEventCounter(boost::bind(&FLPSyncSampler::ResetEventCounter, this));
  boost::thread ackListener(boost::bind(&FLPSyncSampler::ListenForAcks, this));

  int NOBLOCK = fChannels.at("data-out").at(0).fSocket->NOBLOCK;

  uint64_t timeFrameId = 0;

  FairMQChannel& dataOutputChannel = fChannels.at("data-out").at(0);

  while (CheckCurrentState(RUNNING)) {
    std::unique_ptr<FairMQMessage> msg(fTransportFactory->CreateMessage(sizeof(uint64_t)));
    memcpy(msg.get()->GetData(), &timeFrameId, sizeof(uint64_t));

    if (dataOutputChannel.SendAsync(msg) == 0) {
      LOG(ERROR) << "Could not send signal without blocking";
    }

    fTimeframeRTT[timeFrameId].start = boost::posix_time::microsec_clock::local_time();

    if (++timeFrameId == UINT64_MAX - 1) {
      timeFrameId = 0;
    }

    --fEventCounter;

    while (fEventCounter == 0) {
      boost::this_thread::sleep(boost::posix_time::milliseconds(1));
    }
  }

  try {
    resetEventCounter.interrupt();
    resetEventCounter.join();
    ackListener.interrupt();
    ackListener.join();
  } catch(boost::thread_resource_error& e) {
    LOG(ERROR) << e.what();
  }
}

void FLPSyncSampler::ListenForAcks()
{
  FairMQPoller* poller = fTransportFactory->CreatePoller(fChannels["ack-in"]);

  uint64_t id = 0;

  string name = to_iso_string(boost::posix_time::microsec_clock::local_time()).substr(0, 20);
  ofstream ofsFrames(name + "-frames.log");
  ofstream ofsTimes(name + "-times.log");

  while (CheckCurrentState(RUNNING)) {
    try {
      poller->Poll(100);

      if (poller->CheckInput(0)) {
        std::unique_ptr<FairMQMessage> idMsg(fTransportFactory->CreateMessage());

        if (fChannels.at("ack-in").at(0).Receive(idMsg) > 0) {
          id = *(reinterpret_cast<uint64_t*>(idMsg.get()->GetData()));
          fTimeframeRTT.at(id).end = boost::posix_time::microsec_clock::local_time();
          // store values in a file
          ofsFrames << id << "\n";
          ofsTimes  << (fTimeframeRTT.at(id).end - fTimeframeRTT.at(id).start).total_microseconds() << "\n";

          LOG(INFO) << "Timeframe #" << id << " acknowledged after "
                    << (fTimeframeRTT.at(id).end - fTimeframeRTT.at(id).start).total_microseconds() << " μs.";
        }
      }
    } catch (boost::thread_interrupted&) {
      LOG(DEBUG) << "Acknowledgement listener thread interrupted";
      break;
    }
  }

  ofsFrames.close();
  ofsTimes.close();

  delete poller;
}

void FLPSyncSampler::ResetEventCounter()
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

void FLPSyncSampler::SetProperty(const int key, const string& value)
{
  switch (key) {
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

string FLPSyncSampler::GetProperty(const int key, const string& default_ /*= ""*/)
{
  switch (key) {
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}

void FLPSyncSampler::SetProperty(const int key, const int value)
{
  switch (key) {
    case EventRate:
      fEventRate = value;
      break;
    default:
      FairMQDevice::SetProperty(key, value);
      break;
  }
}

int FLPSyncSampler::GetProperty(const int key, const int default_ /*= 0*/)
{
  switch (key) {
    case EventRate:
      return fEventRate;
    default:
      return FairMQDevice::GetProperty(key, default_);
  }
}
