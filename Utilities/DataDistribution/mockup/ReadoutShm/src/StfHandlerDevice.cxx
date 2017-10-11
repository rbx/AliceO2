// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "ReadoutShm/StfHandlerDevice.h"
#include "ReadoutShm/ReadoutO2DataModel.h"

#include <options/FairMQProgOptions.h>
#include <shmem/FairMQRegionSHM.h>
#include <FairMQLogger.h>

#include <chrono>
#include <thread>

namespace o2 { namespace DataDistribution { namespace mockup {

StfHandlerDevice::StfHandlerDevice() : O2Device{} {}

StfHandlerDevice::~StfHandlerDevice() {}

void StfHandlerDevice::InitTask() {
  mInputChannelName =
      GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  // mOutputChannelName =
  // GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
}

bool StfHandlerDevice::ConditionalRun() { return false; }
}
}
} /* namespace o2::DataDistribution::mockup */
