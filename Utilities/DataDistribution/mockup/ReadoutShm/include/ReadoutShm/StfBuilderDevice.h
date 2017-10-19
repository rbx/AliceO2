// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_MOCKUP_STFBUILDER_DEVICE_H_
#define ALICEO2_MOCKUP_STFBUILDER_DEVICE_H_

#include "O2Device/O2Device.h"
#include "ReadoutShm/ReadoutO2DataModel.h"

#include <deque>
#include <mutex>
#include <condition_variable>
#include <deque>

class FairMQRegionSHM;

namespace o2 { namespace DataDistribution { namespace mockup {

class StfBuilderDevice : public Base::O2Device
{
public:
  static constexpr const char* OptionKeyInputChannelName = "input-channel-name";
  static constexpr const char* OptionKeyOutputChannelName = "output-channel-name";

  /// Default constructor
  StfBuilderDevice();

  /// Default destructor
  ~StfBuilderDevice();

  void InitTask() final;

protected:

  void PreRun() final;
  void PostRun() final;
  bool ConditionalRun() final;

  void StfOutputThread();

  std::string      mInputChannelName;
  std::string      mOutputChannelName;
  std::vector<FairMQMessagePtr> mMessages;

  std::thread                                 mOutputThread;
  std::deque<std::vector<FairMQMessagePtr>>   mStfs;
  std::mutex                                  mStfLock;

  const size_t cStfSize = (50ULL << 20); /* 50MiB */
  const size_t cChunkSize = (8 << 10); /* 8kiB */
};

} } } /* namespace o2::DataDistribution::mockup */

#endif /* ALICEO2_MOCKUP_STFBUILDER_DEVICE_H_ */
