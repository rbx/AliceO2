/**
 * FLPexSampler.h
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko
 */

#ifndef ALICEO2_DEVICES_FLPEXSAMPLER_H_
#define ALICEO2_DEVICES_FLPEXSAMPLER_H_

#include <string>

#include "FairMQDevice.h"

namespace AliceO2 {
namespace Devices {

class FLPexSampler : public FairMQDevice
{
  public:
    FLPexSampler();
    virtual ~FLPexSampler();

  protected:
    virtual void Run();
};

} // namespace Devices
} // namespace AliceO2

#endif
