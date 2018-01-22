// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "Common/ReadoutDataModel.h"
#include "Common/SubTimeFrameDataModel.h"

#include <map>
#include <iterator>
#include <algorithm>

namespace o2 {
namespace DataDistribution {

////////////////////////////////////////////////////////////////////////////////
/// EquipmentHBFrames
////////////////////////////////////////////////////////////////////////////////

EquipmentHBFrames::EquipmentHBFrames(int pFMQChannelId, const EquipmentIdentifier &pHdr)
{
  mHeader = make_channel_ptr<EquipmentHeader>(pFMQChannelId);

  mHeader->dataDescription = pHdr.mDataDescription;
  mHeader->dataOrigin = pHdr.mDataOrigin;
  mHeader->subSpecification = pHdr.mSubSpecification;
  mHeader->headerSize = sizeof(EquipmentHBFrames);
}

const EquipmentIdentifier EquipmentHBFrames::getEquipmentIdentifier() const
{
  EquipmentIdentifier lEquipId;
  lEquipId.mDataDescription = mHeader->dataDescription;
  lEquipId.mDataOrigin = mHeader->dataOrigin;
  lEquipId.mSubSpecification = mHeader->subSpecification;

  return lEquipId;
}

void EquipmentHBFrames::addHBFrames(std::vector<FairMQMessagePtr> &&pHBFrames)
{
  std::move(
    pHBFrames.begin(),
    pHBFrames.end(),
    std::back_inserter(mHBFrames)
  );

  pHBFrames.clear();

  // Update payload count
  mHeader->payloadSize = mHBFrames.size();
}

std::uint64_t EquipmentHBFrames::getDataSize() const
{
  std::uint64_t lDataSize = 0;
  for (const auto& lHBFrame : mHBFrames) {
    lDataSize += lHBFrame->GetSize();
    lHBFrame->GetData();
  }
  return lDataSize;
}

////////////////////////////////////////////////////////////////////////////////
/// SubTimeFrame
////////////////////////////////////////////////////////////////////////////////
SubTimeFrame::SubTimeFrame(int pFMQChannelId, uint64_t pStfId)
{
  mFMQChannelId = pFMQChannelId;

  mHeader = make_channel_ptr<SubTimeFrameHeader>(mFMQChannelId);

  mHeader->mId = pStfId;
  mHeader->headerSize = sizeof(SubTimeFrameHeader);
  mHeader->dataDescription = o2::Header::gDataDescriptionSubTimeFrame;
  mHeader->dataOrigin = o2::Header::gDataOriginFLP;
  mHeader->payloadSerializationMethod = o2::Header::gSerializationMethodNone; // Stf serialization?
  mHeader->payloadSize = 0;                                                   // to hold # of CRUs in the FLP
}

void SubTimeFrame::addHBFrames(const ReadoutSubTimeframeHeader &pHdr, std::vector<FairMQMessagePtr> &&pHBFrames)
{
  // FIXME: proper equipment specification
  EquipmentIdentifier lEquipId;
  lEquipId.mDataDescription = o2::Header::gDataDescriptionRawData;
  lEquipId.mDataOrigin = o2::Header::gDataOriginTPC;
  lEquipId.mSubSpecification = pHdr.linkId;

  if (mReadoutData.find(lEquipId) == mReadoutData.end()) {
    /* add a HBFrame collection for the new equipment */
    mReadoutData.emplace(
      std::piecewise_construct,
      std::make_tuple(lEquipId),
      std::make_tuple(mFMQChannelId, lEquipId)
    );
  }

  mReadoutData.at(lEquipId).addHBFrames(std::move(pHBFrames));

  // update the count
  mHeader->payloadSize = mReadoutData.size();
}

std::uint64_t SubTimeFrame::getDataSize() const
{
  std::uint64_t lDataSize = 0;

  for (auto& lReadoutDataKey : mReadoutData) {
    auto& lHBFrameData = lReadoutDataKey.second;
    lDataSize += lHBFrameData.getDataSize();
  }

  return lDataSize;
}

}
} /* o2::DataDistribution */
