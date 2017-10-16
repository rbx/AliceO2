// Copyright CERN and copyright holders of ALICE O2. This software is
// distributed under the terms of the GNU General Public License v3 (GPL
// Version 3), copied verbatim in the file "COPYING".
//
// See http://alice-o2.web.cern.ch/license for full licensing information.
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
/**
 * RawSTFBuilder.h
 *
 * @since 2014-10-10
 * @author A. Rybalchenko
 */

#ifndef ALICEO2_UTILITIES_RAWSTFBUILDER_H
#define ALICEO2_UTILITIES_RAWSTFBUILDER_H

#include <string>

#include "FairMQDevice.h"

class RawSTFBuilder : public FairMQDevice
{
  public:
    RawSTFBuilder();
    virtual ~RawSTFBuilder();

  protected:
    int fMsgSize;

    virtual void InitTask();
    virtual void Run();
};

#endif /* ALICEO2_UTILITIES_RAWSTFBUILDER_H */
