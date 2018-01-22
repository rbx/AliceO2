// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#include "Common/SubTimeFrameUtils.h"
#include "Common/SubTimeFrameVisitors.h"
#include "Common/SubTimeFrameDataModel.h"
#include "Common/DataModelUtils.h"

#include <O2Device/O2Device.h>

#include <stdexcept>

#include <vector>
#include <deque>

namespace o2 {
namespace DataDistribution {

////////////////////////////////////////////////////////////////////////////////
/// DataOriginSplitter
////////////////////////////////////////////////////////////////////////////////
void DataIdentifierSplitter::visit(EquipmentHBFrames&)
{
  /* identifier filtering only */
}

void DataIdentifierSplitter::visit(SubTimeFrame& pStf)
{
  std::vector<EquipmentIdentifier> lToErase;

  if (mDataIdentifier.dataOrigin == gDataOriginAny) {
    mSubTimeFrame = std::move(pStf);
  } else if (mDataIdentifier.dataDescription == gDataDescriptionAny) {
    // filter any source with requested origin
    for (auto &lKeyData : pStf.mReadoutData) {
      const EquipmentIdentifier& lIden = lKeyData.first;
      if (lIden.mDataOrigin == mDataIdentifier.dataOrigin) {
        // use the equipment identifier of the object
        mSubTimeFrame.mReadoutData[lIden] = std::move(lKeyData.second);
        lToErase.emplace_back(lIden);
      }
    }

  } else {
    /* find exact match */
    for (auto &lKeyData : pStf.mReadoutData) {
      const EquipmentIdentifier& lIden = lKeyData.first;
      if (lIden.mDataOrigin == mDataIdentifier.dataOrigin &&
        lIden.mDataDescription == mDataIdentifier.dataDescription) {
        // use the equipment identifier of the object
        mSubTimeFrame.mReadoutData[lIden] = std::move(lKeyData.second);
        lToErase.emplace_back(lIden);
      }
    }
  }

  // erase forked elements
  for (auto &lIden : lToErase)
    pStf.mReadoutData.erase(lIden);

  // update element counts
  pStf.mHeader->payloadSize = pStf.mReadoutData.size();
  mSubTimeFrame.mHeader->payloadSize = mSubTimeFrame.mReadoutData.size();
}


SubTimeFrame DataIdentifierSplitter::split(SubTimeFrame& pStf, const DataIdentifier& pDataIdent, const int pChanId)
{
  mSubTimeFrame = SubTimeFrame(pChanId, pStf.Header().mId);
  mDataIdentifier = pDataIdent;

  pStf.accept(*this);

  return std::move(mSubTimeFrame);
}


}
} /* o2::DataDistribution */
