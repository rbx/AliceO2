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

  bool ConditionalRun() final;

  std::string      mInputChannelName;
  std::string      mOutputChannelName;
};

} } } /* namespace o2::DataDistribution::mockup */

#endif /* ALICEO2_MOCKUP_STFBUILDER_DEVICE_H_ */
