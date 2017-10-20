// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_MOCKUP_READOUT_O2DATAMODEL_H_
#define ALICEO2_MOCKUP_READOUT_O2DATAMODEL_H_

#include "Headers/DataHeader.h"

#include <vector>

class FairMQUnmanagedRegion;

namespace o2 { namespace DataDistribution { namespace mockup {

using DataHeader = o2::Header::DataHeader;

struct CruDmaPacket {
  FairMQUnmanagedRegion *mDataSHMRegion = nullptr;
  size_t                mDataOffset;
  size_t                mDataSize;

  FairMQUnmanagedRegion *mDescSHMRegion = nullptr;
  size_t                mDescOffset;
  size_t                mDescSize;
};


struct ReadoutO2Data {
  std::vector<CruDmaPacket> mReadoutData;
};


} } } /* o2::DataDistribution::mockup */


#endif /* ALICEO2_MOCKUP_READOUT_O2DATAMODEL_H_ */
