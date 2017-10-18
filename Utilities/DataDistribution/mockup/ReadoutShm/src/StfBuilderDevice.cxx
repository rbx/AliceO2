// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "ReadoutShm/StfBuilderDevice.h"
#include "ReadoutShm/ReadoutO2DataModel.h"

#include <options/FairMQProgOptions.h>
#include <shmem/FairMQRegionSHM.h>
#include <FairMQLogger.h>

#include <chrono>
#include <thread>

namespace o2 { namespace DataDistribution { namespace mockup {

StfBuilderDevice::StfBuilderDevice()
    : O2Device{}
    , mMessages()
    {}

StfBuilderDevice::~StfBuilderDevice() {}

void StfBuilderDevice::InitTask() {
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mOutputChannelName = GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
}

bool StfBuilderDevice::ConditionalRun() {
    FairMQMessagePtr header(NewMessageFor(mInputChannelName, 0));

    Receive(header, mInputChannelName);

    DataHeader* mO2DataHeader = static_cast<DataHeader*>(header->GetData());
    for (int i = 0; i < mO2DataHeader->payloadSize; ++i) {
        FairMQMessagePtr msg(NewMessageFor(mInputChannelName, 0));
        Receive(msg, mInputChannelName);
        LOG(INFO) << std::hex << *static_cast<char*>(msg->GetData()) << std::dec;
        mMessages.push_back(std::move(msg));
    }

    return true;
}

}
}
} /* namespace o2::DataDistribution::mockup */
