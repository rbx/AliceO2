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
#include <options/FairMQProgOptions.h>

#include "runFairMQDevice.h"

namespace bpo = boost::program_options;

void addCustomOptions(bpo::options_description &options) {
  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::OptionKeyOutputChannelName,
      bpo::value<std::string>()->default_value("readout-output"),
      "Name of the readout output channel");

  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::
          OptionKeyReadoutDataRegionName,
      bpo::value<std::string>()->default_value("data-shm-segment"),
      "Name of the data shm segment");

  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::
          OptionKeyReadoutDataRegionSize,
      bpo::value<std::size_t>()->default_value(1ULL << 30 /* 1GiB */),
      "Size of the data shm segment");

  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::
          OptionKeyReadoutDescRegionName,
      bpo::value<std::string>()->default_value("desc-shm-segment"),
      "Name of the desc shm segment");

  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::
          OptionKeyReadoutDescRegionSize,
      bpo::value<std::size_t>()->default_value(1ULL << 26 /* 64MiB */),
      "Size of the desc shm segment");

  options.add_options()(
      o2::DataDistribution::mockup::ReadoutDevice::
          OptionKeyReadoutSuperpageSize,
      bpo::value<size_t>()->default_value(1ULL << 20 /* 1MiB */),
      "CRU superpage size");
}

FairMQDevicePtr getDevice(const FairMQProgOptions & /*config*/) {
  return new o2::DataDistribution::mockup::ReadoutDevice();
}
