// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_SUBTIMEFRAME_DATAMODEL_H_
#define ALICEO2_SUBTIMEFRAME_DATAMODEL_H_

#include "Common/Utilities.h"
#include "Common/DataModelUtils.h"
#include "Common/ReadoutDataModel.h"

#include <O2Device/O2Device.h>
#include <Headers/DataHeader.h>

#include <map>
#include <stdexcept>

namespace o2 {
namespace DataDistribution {

using namespace o2::Base;
using namespace o2::Header;

struct EquipmentIdentifier
{
  DataDescription                   mDataDescription;
  DataOrigin                        mDataOrigin;
  DataHeader::SubSpecificationType  mSubSpecification; /* uint64_t */

  bool operator<(const EquipmentIdentifier &other) const {
    if (mDataDescription < other.mDataDescription)
      return true;
    else if (mDataDescription == other.mDataDescription && mSubSpecification < other.mSubSpecification)
      return true;
    else if (mDataDescription == other.mDataDescription && mSubSpecification == other.mSubSpecification && mSubSpecification < other.mSubSpecification)
      return true;
    else
      return false;
  }

  const std::string info() const {
    return std::string("DataDescription: ") + std::string(mDataDescription.str) +
          std::string(" DataOrigin: ") + std::string(mDataOrigin.str) +
          std::string(" SubSpecification: ") + std::to_string(mSubSpecification);
  }
};

////////////////////////////////////////////////////////////////////////////////
/// Visitor friends
////////////////////////////////////////////////////////////////////////////////
#define DECLARE_STF_FRIENDS                         \
  friend class InterleavedHdrDataSerializer;        \
  friend class InterleavedHdrDataDeserializer;      \
  friend class HdrDataSerializer;                   \
  friend class HdrDataDeserializer;                 \
  friend class DataIdentifierSplitter;

////////////////////////////////////////////////////////////////////////////////
/// EquipmentHBFrames
////////////////////////////////////////////////////////////////////////////////
struct EquipmentHeader : public DataHeader {
  //
};

class EquipmentHBFrames : public IDataModelObject {
  DECLARE_STF_FRIENDS
public:
  EquipmentHBFrames(int pFMQChannelId, const EquipmentIdentifier &pHdr);
  EquipmentHBFrames() = default;
  ~EquipmentHBFrames() = default;
  // no copy
  EquipmentHBFrames(const EquipmentHBFrames&) = delete;
  EquipmentHBFrames& operator=(const EquipmentHBFrames&) = delete;
  // default move
  EquipmentHBFrames(EquipmentHBFrames&& a) = default;
  EquipmentHBFrames& operator=(EquipmentHBFrames&& a) = default;

  void accept(ISubTimeFrameVisitor& v) override { v.visit(*this); };

  void addHBFrames(std::vector<FairMQMessagePtr> &&pHBFrames);
  std::uint64_t getDataSize() const;
  const EquipmentIdentifier getEquipmentIdentifier() const;

  const EquipmentHeader& Header() const { return *mHeader; }

private:
  ChannelPtr<EquipmentHeader>      mHeader;
  std::vector<FairMQMessagePtr>   mHBFrames;
};

////////////////////////////////////////////////////////////////////////////////
/// SubTimeFrame
////////////////////////////////////////////////////////////////////////////////
using TimeFrameIdType = std::uint64_t;
using SubTimeFrameIdType = TimeFrameIdType;

struct SubTimeFrameHeader : public DataHeader {
  TimeFrameIdType mId;
  std::uint32_t mMaxHBFrames;
};

class SubTimeFrame : public IDataModelObject {
  DECLARE_STF_FRIENDS
public:
  SubTimeFrame(int pFMQChannelId, std::uint64_t pStfId);
  SubTimeFrame() = default;
  ~SubTimeFrame() = default;
  // no copy
  SubTimeFrame(const SubTimeFrame&) = delete;
  SubTimeFrame& operator=(const SubTimeFrame&) = delete;
  // default move
  SubTimeFrame(SubTimeFrame&& a) = default;
  SubTimeFrame& operator=(SubTimeFrame&& a) = default;

  // adopt all data from a
  SubTimeFrame& operator+=(SubTimeFrame&& a);

  void accept(ISubTimeFrameVisitor& v) override { v.visit(*this); };

  // TODO: add proper equip IDs
  void addHBFrames(const ReadoutSubTimeframeHeader &pHdr, std::vector<FairMQMessagePtr> &&pHBFrames);
  std::uint64_t getDataSize() const;

  const SubTimeFrameHeader& Header() const { return *mHeader; }

private:

  ChannelPtr<SubTimeFrameHeader> mHeader;

  // 1. map: EquipmentIdentifier -> Data (e.g. (TPC, CLUSTERS) => (All cluster data) )
  std::map<EquipmentIdentifier, EquipmentHBFrames> mReadoutData;

  // save the FMQChannelID
  int mFMQChannelId = -1;
};


}
} /* o2::DataDistribution */

#endif /* ALICEO2_SUBTIMEFRAME_DATAMODEL_H_ */
