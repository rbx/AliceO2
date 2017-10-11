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
  if (mDataRegion)
    delete mDataRegion;

  if (mDescRegion)
    delete mDescRegion;
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

  if (mDataRegion == nullptr || mDescRegion == nullptr ||
      oldmDataRegionName != mDataRegionName ||
      oldmDataRegionSize != mDataRegionSize ||
      oldmDescRegionName != mDescRegionName ||
      oldmDescRegionSize != mDescRegionSize) {

    mO2Interface.clear();

    // Open SHM regions (segments)
    if (mDataRegion)
      delete mDataRegion;

    if (mDescRegion)
      delete mDescRegion;

    mDataRegion = new FairMQRegionSHM(mDataRegionSize); // TODO: name
    mDescRegion = new FairMQRegionSHM(mDescRegionSize); // TODO: name

    //
    auto *data = mDataRegion->GetData();
    mCRUMemoryHandler.init(mDataRegion, mDescRegion, mSuperpageSize);
  }
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
    ReadoutO2Data linkO2Data;
    linkO2Data.mO2DataHeader.headerSize = sizeof(DataHeader);
    linkO2Data.mO2DataHeader.flags = 0;
    linkO2Data.mO2DataHeader.dataDescription =
        o2::Header::gDataDescriptionRawData;
    linkO2Data.mO2DataHeader.dataOrigin = o2::Header::gDataOriginTPC;
    linkO2Data.mO2DataHeader.payloadSerializationMethod =
        o2::Header::gSerializationMethodNone;
    linkO2Data.mO2DataHeader.subSpecification = link;
    linkO2Data.mO2DataHeader.payloadSize =
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

      // linkO2Data.mO2DataHeader.payloadSize += desc->mRawDataSize;
      linkO2Data.mO2DataHeader.payloadSize += cCruDmaPacketSize;

      linkO2Data.mReadoutData.emplace_back(CruDmaPacket{
        mDataRegion,
        size_t(sp.mDataVirtualAddress + (packet * cCruDmaPacketSize) -
          static_cast<char *>(mDataRegion->GetData())),
        cCruDmaPacketSize, // This should be taken from desc->mRawDataSize
                           // (filled by the CRU)
        mDescRegion,
        size_t(reinterpret_cast<const char *>(desc) -
          static_cast<char *>(mDescRegion->GetData())),
        sizeof (RawDmaPacketDesc)
      });

      // mark CruPackets as used
      mCRUMemoryHandler.get_data_buffer(sp.mDataVirtualAddress + (packet * cCruDmaPacketSize), cCruDmaPacketSize);
    }

    // LOG(INFO) << "Sending a superpage for CRU-link " << link << " containing
    // " << linkO2Data.mReadoutData.size() << " packets.";
    mO2Interface.put(std::move(linkO2Data));
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

void CRUMemoryHandler::init(FairMQRegionSHM *dataRegion,
                            FairMQRegionSHM *descRegion, size_t superpageSize)
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

ReadoutO2Interface::ReadoutO2Interface()
{
  mRunning = true;
  mRunner = std::thread(&ReadoutO2Interface::run, this);
}

ReadoutO2Interface::~ReadoutO2Interface()
{
  stop();
  mRunner.join();
}

/// Send CRU-link data to O2
void ReadoutO2Interface::run()
{
  while (mRunning) {
    ReadoutO2Data toSend;
    if (!get(toSend)) { // better not be blocking anywhere else...
      if (!mRunning)
        return;
      else
        continue;
    }

    LOG(INFO) << "Sending a superpage for CRU-link "
              << toSend.mO2DataHeader.subSpecification << " containing "
              << toSend.mReadoutData.size() << " packets.";

    // TODO:
    //  - Send toSend out -> mOutChannelName
    //  - Return superpages mCRUMemoryHandler.put_superpage(virt)
    //  - ...
    //
    //
    //
  }
}

void ReadoutO2Interface::stop()
{
  std::lock_guard<std::mutex> lock(mLock);
  mRunning = false;
  mDataCond.notify_all();
}

void ReadoutO2Interface::clear()
{
  std::lock_guard<std::mutex> lock(mLock);
  mO2Data.clear();
}

/// Called from CRU-DMA interrupt routine
void ReadoutO2Interface::put(ReadoutO2Data &&rd)
{
  std::lock_guard<std::mutex> lock(mLock);
  mO2Data.emplace_back(std::move(rd));
  mDataCond.notify_one();
}

/// Called from ReadoutO2Interface::roon() send loop
bool ReadoutO2Interface::get(ReadoutO2Data &rd)
{
  std::unique_lock<std::mutex> lock(mLock);

  while (mO2Data.empty() && mRunning)
    mDataCond.wait(lock);

  if (!mRunning)
    return false;

  rd = mO2Data.front();
  mO2Data.pop_front();
  return true;
}

} } } /* namespace o2::DataDistribution::mockup */
