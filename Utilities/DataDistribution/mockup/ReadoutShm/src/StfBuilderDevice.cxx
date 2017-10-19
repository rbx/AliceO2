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
    {
    }

StfBuilderDevice::~StfBuilderDevice() {
    mOutputThread.join();
}

void StfBuilderDevice::InitTask() {
  mInputChannelName = GetConfig()->GetValue<std::string>(OptionKeyInputChannelName);
  mOutputChannelName = GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
}

void StfBuilderDevice::PreRun()
{
    mOutputThread = std::thread(&StfBuilderDevice::StfOutputThread, this);
}

void StfBuilderDevice::PostRun()
{
    mOutputThread.join(); // stopped by CheckCurrentState(RUNNING)
}

bool StfBuilderDevice::ConditionalRun()
{
    FairMQMessagePtr header(NewMessageFor(mInputChannelName, 0));

    Receive(header, mInputChannelName);

    DataHeader mO2DataHeader;
    // boost is not aligning properly these buffers.
    // without this memcopy, mO2DataHeader->payloadSize gives wrong number
    memcpy(&mO2DataHeader, header->GetData(), header->GetSize());

    if (mO2DataHeader.payloadSize > 128) {
        LOG(ERROR) << "PayloadSize too large! Stopping... " << mO2DataHeader.payloadSize;
        return false;
    }

    // LOG(INFO) << "Receiving " << mO2DataHeader.payloadSize << " payloads.";

    for (int i = 0; i < mO2DataHeader.payloadSize; ++i) {
        FairMQMessagePtr msg(NewMessageFor(mInputChannelName, 0));

        Receive(msg, mInputChannelName);

        // LOG(INFO) << i << " " << std::hex << *static_cast<uint64_t*>(msg->GetData()) << std::dec;

        mMessages.push_back(std::move(msg));
    }

    /* Collect min of cStfSize of data chunks and forward the STF */
    if (mMessages.size() * cChunkSize > cStfSize) {
        LOG(INFO) << "StfBuilder-I: queued STF of size " << mMessages.size() * cChunkSize
            << " and " << mMessages.size() << " chunks";

        {
            std::lock_guard<std::mutex> lock(mStfLock);
            mStfs.emplace_back(std::move(mMessages));
        }
        mMessages.clear();
    }

    return true;
}

void StfBuilderDevice::StfOutputThread()
{
    // std::this_thread::sleep_for(std::chrono::seconds(1)); //

    while (CheckCurrentState(RUNNING)) {
        std::vector<FairMQMessagePtr> lStf;
        {
            std::lock_guard<std::mutex> lock(mStfLock);
            if (mStfs.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue; // TODO: block on a condvar
            }
            lStf = std::move(mStfs.front());
            mStfs.pop_front();
        }

        LOG(INFO) << "StfBuilder-O: sending STF of size " << lStf.size() * cChunkSize
            << " and " << lStf.size() << " chunks";

        DataHeader lStfO2DataHeader;
        lStfO2DataHeader.headerSize = sizeof(DataHeader);
        lStfO2DataHeader.flags = 0;
        lStfO2DataHeader.dataDescription = o2::Header::gDataDescriptionSubTimeFrame;
        lStfO2DataHeader.dataOrigin = o2::Header::gDataOriginFLP;
        lStfO2DataHeader.payloadSerializationMethod = o2::Header::gSerializationMethodNone;
        lStfO2DataHeader.subSpecification = 0;
        lStfO2DataHeader.payloadSize = lStf.size();

        /* Send header first */
        FairMQMessagePtr lStfHeader(NewMessageFor(mOutputChannelName, 0, sizeof(lStfO2DataHeader)));
        memcpy(lStfHeader->GetData(), &lStfO2DataHeader, sizeof(lStfO2DataHeader));

        if (Send(lStfHeader, mOutputChannelName) < 0) {
          LOG(ERROR) << "could not send STF DataHeader ";
          return;
        }


        for (auto &msg : lStf) {
            if (Send(msg, mOutputChannelName) < 0) {
              LOG(ERROR) << "could not send data ";
              return;
            }
        }
    }

    LOG(INFO) << "StfBuilder-O: Thread exiting";
}

} } } /* namespace o2::DataDistribution::mockup */
