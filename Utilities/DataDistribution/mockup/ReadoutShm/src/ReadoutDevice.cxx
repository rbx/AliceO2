// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "ReadoutShm/ReadoutDevice.h"
#include "ReadoutShm/ReadoutO2DataModel.h"

#include <options/FairMQProgOptions.h>
#include <shmem/FairMQRegionSHM.h>
#include <FairMQLogger.h>

#include <chrono>
#include <thread>

namespace o2 { namespace DataDistribution { namespace mockup {

ReadoutDevice::ReadoutDevice() : O2Device{} {}

ReadoutDevice::~ReadoutDevice() {
}

void ReadoutDevice::InitTask()
{
  auto oldmDataRegionName = mDataRegionName;
  auto oldmDataRegionSize = mDataRegionSize;
  auto oldmDescRegionName = mDescRegionName;
  auto oldmDescRegionSize = mDescRegionSize;
  mOutChannelName =
      GetConfig()->GetValue<std::string>(OptionKeyOutputChannelName);
  mDataRegionName =
      GetConfig()->GetValue<std::string>(OptionKeyReadoutDataRegionName);
  mDataRegionSize =
      GetConfig()->GetValue<std::size_t>(OptionKeyReadoutDataRegionSize);
  mDescRegionName =
      GetConfig()->GetValue<std::string>(OptionKeyReadoutDescRegionName);
  mDescRegionSize =
      GetConfig()->GetValue<std::size_t>(OptionKeyReadoutDescRegionSize);
  mSuperpageSize =
      GetConfig()->GetValue<std::size_t>(OptionKeyReadoutSuperpageSize);

  if (mSuperpageSize & (mSuperpageSize - 1)) {
    LOG(ERROR) << "Superpage size must be power of 2: " << mSuperpageSize;
    exit (-1);
  }

  mDataRegion.reset();
  mDescRegion.reset();

  // Open SHM regions (segments)

  mDataRegion = FairMQRegionPtr(new FairMQRegionSHM(mDataRegionSize));
  mDescRegion = FairMQRegionPtr(new FairMQRegionSHM(mDescRegionSize));

  //
  auto *data = mDataRegion->GetData();
  mCRUMemoryHandler.init(mDataRegion.get(), mDescRegion.get(), mSuperpageSize);

}

bool ReadoutDevice::ConditionalRun()
{
  static const size_t cStfPerSec = 25;    /* (50) Parametrize this */
  static const size_t cStfSize = 35 << 20; /* (50MiB) Parametrize this */

  static const size_t cNumCruLinks = 12;
  static const size_t cCruDmaPacketSize = 8 << 10; /* 8kiB */
  // how many 8kB segments

  const auto num_sp_in_stf = cStfSize / mSuperpageSize;
  const auto numDescInSp = mSuperpageSize / cCruDmaPacketSize;

  // here we assume the CRU produced cNumCruLinks x superpages, each with
  // associated
  // descriptors
  const auto sleepTime = std::chrono::microseconds(
      1000000 * (cNumCruLinks * mSuperpageSize) / (cStfPerSec * cStfSize));

  for (auto link = 0; link < cNumCruLinks; link++) {
    CRUSuperpage sp;
    if (!mCRUMemoryHandler.get_superpage(sp)) {
      LOG(ERROR) << "Losing data! No superpages available!";
      continue;
    }

    // The sp is now filled by the CRU-DMA.
    // The software should filter out HBFs that are not accepted by HBMap

    // Each channel is reported separately to the O2

    FairMQMessagePtr header(NewMessageFor(OptionKeyOutputChannelName, 0, sizeof(DataHeader)));

    DataHeader* mO2DataHeader = new (header->GetData()) DataHeader();
    ReadoutO2Data linkO2Data;
    mO2DataHeader->headerSize = sizeof(DataHeader);
    mO2DataHeader->flags = 0;
    mO2DataHeader->dataDescription =
        o2::Header::gDataDescriptionRawData;
    mO2DataHeader->dataOrigin = o2::Header::gDataOriginTPC;
    mO2DataHeader->payloadSerializationMethod =
        o2::Header::gSerializationMethodNone;
    mO2DataHeader->subSpecification = link;
    mO2DataHeader->payloadSize =
        0; // TODO: how to calculate this? Sum of all accepted DmaPackets

    RawDmaPacketDesc *desc =
      reinterpret_cast<RawDmaPacketDesc*>(sp.mDescVirtualAddress);
    for (auto packet = 0; packet < numDescInSp; packet++, desc++) {

      // Real-world scenario: CRU marks some of the DMA packet slots as invalid.
      // Simulate this by making ~10% of them invalid.
      {
        desc->mValidHBF = (rand() % 100 > 10) ? true : false;
      }

      if (!desc->mValidHBF)
        continue;

      mO2DataHeader->payloadSize += cCruDmaPacketSize;

      linkO2Data.mReadoutData.emplace_back(CruDmaPacket{
        mDataRegion.get(),
        size_t(sp.mDataVirtualAddress + (packet * cCruDmaPacketSize) -
          static_cast<char *>(mDataRegion->GetData())),
        cCruDmaPacketSize, // This should be taken from desc->mRawDataSize
                           // (filled by the CRU)
        mDescRegion.get(),
        size_t(reinterpret_cast<const char *>(desc) -
          static_cast<char *>(mDescRegion->GetData())),
        sizeof (RawDmaPacketDesc)
      });

      // mark CruPackets as used
      mCRUMemoryHandler.get_data_buffer(sp.mDataVirtualAddress + (packet * cCruDmaPacketSize), cCruDmaPacketSize);
    }

    mO2DataHeader->payloadSize = linkO2Data.mReadoutData.size();

    Send(header, OptionKeyOutputChannelName);

    for (const auto& d : linkO2Data.mReadoutData) {
      FairMQMessagePtr msg(NewMessageFor(OptionKeyOutputChannelName,
                                         0,
                                         mDataRegion,
                                         static_cast<char*>(d.mDataSHMRegion->GetData()) + d.mDataOffset,
                                         d.mDataSize));

      Send(msg, OptionKeyOutputChannelName);
    }



    // LOG(INFO) << "Sending a superpage for CRU-link " << link << " containing
    // " << linkO2Data.mReadoutData.size() << " packets.";


  }

  std::this_thread::sleep_for(sleepTime);
  return true;
}

void CRUMemoryHandler::teardown()
{
  std::lock_guard<std::mutex> lock(mLock);
  mVirtToSuperpage.clear();
  mSuperpages = std::stack<CRUSuperpage>();
}

void CRUMemoryHandler::init(FairMQRegion *dataRegion,
                            FairMQRegion *descRegion, size_t superpageSize)
{
  teardown();

  //
  // virt -> bus mapping with IOMMU goes here
  //
  mSuperpageSize = superpageSize;
  mDataStartAddress = static_cast<char *>(dataRegion->GetData());
  mDataRegionSize = dataRegion->GetSize();

  char *descMem = static_cast<char *>(descRegion->GetData());
  const size_t descSize = descRegion->GetSize();

  const auto cntSuperpages = mDataRegionSize / mSuperpageSize;
  const auto cntDmaBufPerSuperpage = mSuperpageSize / (8 << 10);
  const auto minDescSize = cntSuperpages * cntDmaBufPerSuperpage
    * sizeof(RawDmaPacketDesc);

  if (descSize < minDescSize) {
    LOG(ERROR) << "Descriptor segment too small (" << descSize
               << "). Should be: " << minDescSize << " bytes.";
    exit(-1);
  }

  // make sure the memory is allocated properly
  std::memset(mDataStartAddress, 0xDA, mDataRegionSize);
  std::memset(descMem, 0xDE, descSize);

  // lock and initialize the empty page queue
  std::lock_guard<std::mutex> lock(mLock);

  for (size_t i = 0; i < cntSuperpages; i++) {
    CRUSuperpage sp{
      i * mSuperpageSize,
      mDataStartAddress + (i * mSuperpageSize),
      nullptr,

      i * sizeof (RawDmaPacketDesc),
      descMem + (i * sizeof(RawDmaPacketDesc)),
      nullptr
    };

    mSuperpages.push(sp);
    mVirtToSuperpage[sp.mDataVirtualAddress] = sp;
  }
}

bool CRUMemoryHandler::get_superpage(CRUSuperpage &sp)
{
  std::lock_guard<std::mutex> lock(mLock);

  if (mSuperpages.empty())
    return false;

  sp = mSuperpages.top();
  mSuperpages.pop();
  return true;
}

void CRUMemoryHandler::put_superpage(const char *spVirtAddr)
{
  std::lock_guard<std::mutex> lock(mLock);

  mSuperpages.push(mVirtToSuperpage[spVirtAddr]);
}

void CRUMemoryHandler::get_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize)
{
  const char *spStartAddr = reinterpret_cast<char*>((uintptr_t)dataBufferAddr &
    ~((uintptr_t)mSuperpageSize - 1));

  std::lock_guard<std::mutex> lock(mLock);

  // make sure the data buffer is not already in use
  if (mUsedSuperPages[spStartAddr].count(dataBufferAddr) != 0) {
    LOG(ERROR) << "Data buffer is already in the used list! "
               << std::hex << (uintptr_t)dataBufferAddr << std::dec;
    return;
  }

  mUsedSuperPages[spStartAddr][dataBufferAddr] = dataBuffSize;
}

void CRUMemoryHandler::put_data_buffer(const char *dataBufferAddr, const std::size_t dataBuffSize)
{
  const char *spStartAddr = reinterpret_cast<char*>((uintptr_t)dataBufferAddr &
    ~((uintptr_t)mSuperpageSize - 1));

  if (spStartAddr < mDataStartAddress ||
    spStartAddr > mDataStartAddress + mDataRegionSize) {
    LOG(ERROR) << "Returned data buffer outside of the data segment!";
    return;
  }

  std::lock_guard<std::mutex> lock(mLock);

  if (mUsedSuperPages.count(spStartAddr) == 0) {
    LOG(ERROR) << "Returned data buffer is not in the list of used superpages!";
    return;
  }

  auto &spBuffMap = mUsedSuperPages[spStartAddr];

  if (spBuffMap.count(dataBufferAddr) == 0) {
    LOG(ERROR) << "Returned data buffer is not marked as used within the superpage!";
    return;
  }

  if (spBuffMap[dataBufferAddr] != dataBuffSize) {
    LOG(ERROR) << "Returned data buffer size does not match the records: "
               << spBuffMap[dataBufferAddr] << " != "
               << dataBuffSize << "(recorded != returned)";
    return;
  }

  if (spBuffMap.size() > 1) {
    spBuffMap.erase(dataBufferAddr);
  } else {
    mUsedSuperPages.erase(spStartAddr);
    mSuperpages.push(mVirtToSuperpage[spStartAddr]);
  }
}

} } } /* namespace o2::DataDistribution::mockup */
