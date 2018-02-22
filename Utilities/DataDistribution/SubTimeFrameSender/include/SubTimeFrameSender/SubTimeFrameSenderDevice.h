// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_STF_SENDER_DEVICE_H_
#define ALICEO2_STF_SENDER_DEVICE_H_

#include "O2Device/O2Device.h"
#include "SubTimeFrameSender/SubTimeFrameSenderOutput.h"

#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>

namespace o2 {
namespace DataDistribution {

class StfSenderDevice : public Base::O2Device {
public:
  static constexpr const char* OptionKeyInputChannelName = "input-channel-name";
  static constexpr const char* OptionKeyOutputChannelName = "output-channel-name";
  static constexpr const char* OptionKeyEpnNodeCount = "epn-count";

  /// Default constructor
  StfSenderDevice();

  /// Default destructor
  ~StfSenderDevice() override;

  void InitTask() final;


  const std::string& getOutputChannelName() const { return mOutputChannelName; }

protected:
  void PreRun() final;
  void PostRun() final;
  bool ConditionalRun() final;

  /// Configuration
  std::string mInputChannelName;
  std::string mOutputChannelName;
  std::uint32_t mEpnNodeCount;

  /// Output stage handler
  StfSenderOutputInterface mOutputHandler;
};

}
} /* namespace o2::DataDistribution */

#endif /* ALICEO2_STF_SENDER_DEVICE_H_ */
