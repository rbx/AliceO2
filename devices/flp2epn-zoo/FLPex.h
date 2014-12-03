/**
 * FLPex.h
 *
 * @since 2014-02-24
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#ifndef ALICEO2_DEVICES_FLPEX_H_
#define ALICEO2_DEVICES_FLPEX_H_

#include <string>
#include <queue>

#include "EpnScheduler.h"

#include "FairMQDevice.h"

namespace AliceO2 {
namespace Devices {

class FLPex : public FairMQDevice
{
  public:
    enum {
      SendOffset = FairMQDevice::Last,
      SchedulerAddress,
      EventSize,
      Last
    };

    FLPex();
    virtual ~FLPex();

    void ResetEventCounter();
    virtual void SetProperty(const int key, const std::string& value, const int slot = 0);
    virtual std::string GetProperty(const int key, const std::string& default_ = "", const int slot = 0);
    virtual void SetProperty(const int key, const int value, const int slot = 0);
    virtual int GetProperty(const int key, const int default_ = 0, const int slot = 0);

  protected:
    virtual void Init();
    virtual void Run();

  private:
    int fSendOffset;
    std::queue<FairMQMessage*> fIdBuffer;
    std::queue<FairMQMessage*> fDataBuffer;

    int fEventSize;

    string fSchedulerAddress;
    EpnScheduler fScheduler;
};

} // namespace Devices
} // namespace AliceO2

#endif
