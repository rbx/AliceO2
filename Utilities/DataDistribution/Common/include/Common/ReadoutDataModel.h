// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

#ifndef ALICEO2_READOUT_DATAMODEL_H_
#define ALICEO2_READOUT_DATAMODEL_H_

#include "Common/Utilities.h"

#include "O2Device/O2Device.h"
#include "Headers/DataHeader.h"

#include <vector>
#include <map>
#include <stdexcept>

namespace o2 {
namespace DataDistribution {

using namespace o2::Base;
using namespace o2::Header;

////////////////////////////////////////////////////////////////////////////////
/// CRU Link
////////////////////////////////////////////////////////////////////////////////

// FIXME: copied from Readout/SubTimeframe.h
// definition of a header message for a subtimeframe
// subtimeframe made of 1 message with this header
// followed by 1 message for each heartbeat-frame
// All data come from the same data source (same linkId - but possibly different FEE ids)

struct ReadoutSubTimeframeHeader {
  uint32_t timeframeId; // id of timeframe
  uint32_t numberOfHBF; // number of HB frames (i.e. following messages)
  uint8_t linkId; // common link id of all data in this HBframe
};


}
} /* o2::DataDistribution */

#endif /* ALICEO2_READOUT_DATAMODEL_H_ */
