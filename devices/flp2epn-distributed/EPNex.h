/**
 * EPNex.h
 *
 * @since 2013-01-09
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#ifndef ALICEO2_DEVICES_EPNEX_H_
#define ALICEO2_DEVICES_EPNEX_H_

#include <string>
#include <unordered_map>

#include <boost/date_time/posix_time/posix_time.hpp>

#include "FairMQDevice.h"

namespace AliceO2 {
namespace Devices {

struct timeframeBuffer
{
  int count;
  vector<FairMQMessage*> parts;
  boost::posix_time::ptime startTime;
  boost::posix_time::ptime endTime;
};

class EPNex : public FairMQDevice
{
  public:
    enum {
      HeartbeatIntervalInMs = FairMQDevice::Last,
      NumFLPs,
      Last
    };
    EPNex();
    virtual ~EPNex();

    void PrintBuffer(std::unordered_map<uint64_t,timeframeBuffer> &buffer);

    virtual void SetProperty(const int key, const std::string& value, const int slot = 0);
    virtual std::string GetProperty(const int key, const std::string& default_ = "", const int slot = 0);
    virtual void SetProperty(const int key, const int value, const int slot = 0);
    virtual int GetProperty(const int key, const int default_ = 0, const int slot = 0);

  protected:
    virtual void Run();
    void sendHeartbeats();

    int fHeartbeatIntervalInMs;
    int fNumFLPs;

    int fNumOfDiscardedTimeframes;

    // std::unordered_map<uint64_t,int> fTimeframeCounter;
    // std::unordered_map<uint64_t,vector<FairMQMessage*>*> fTimeframeBuffer;
    std::unordered_map<uint64_t,timeframeBuffer> fTimeframeBuffer;
};

} // namespace Devices
} // namespace AliceO2

#endif
