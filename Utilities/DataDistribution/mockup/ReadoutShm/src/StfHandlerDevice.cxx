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
#include <FairMQLogger.h>

#include <chrono>
#include <thread>

namespace o2 { namespace DataDistribution { namespace mockup {

StfHandlerDevice::StfHandlerDevice()
  : O2Device{}
{}

StfHandlerDevice::~StfHandlerDevice()
{}

void StfHandlerDevice::InitTask() {
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mFreeShmChannelName = GetConfig()->GetValue<std::string>(OptionKeyFreeShmChannelName);
}

bool StfHandlerDevice::ConditionalRun()
{
  FairMQMessagePtr header(NewMessageFor(mInputChannelName, 0));

  Receive(header, mInputChannelName);

  DataHeader mO2DataHeader;
  // boost is not aligning properly these buffers.
  // without this memcopy, mO2DataHeader->payloadSize gives wrong number
  memcpy(&mO2DataHeader, header->GetData(), header->GetSize());

  assert(mO2DataHeader.dataDescription == o2::Header::gDataDescriptionSubTimeFrame);

  LOG(INFO) << "Receiving " << mO2DataHeader.payloadSize << " payloads.";

  for (int i = 0; i < mO2DataHeader.payloadSize; ++i) {
      FairMQMessagePtr msg(NewMessageFor(mInputChannelName, 0));

      Receive(msg, mInputChannelName);

      // LOG(INFO) << i << " " << std::hex << *static_cast<uint64_t*>(msg->GetData()) << std::dec;

      mMessages.emplace_back(std::move(msg));
  }

  LOG(INFO) << "StfHandler-I: received STF consisting of " << mMessages.size() << " messages.";

  // TODO: feeing back-channel
  for (auto &chunk : mMessages) {
    Send(chunk, mFreeShmChannelName);
  }
  LOG(INFO) << "StfHandler-O: freed " << mMessages.size() << " chunks.";
  mMessages.clear();

  return true;
}

} } } /* namespace o2::DataDistribution::mockup */
