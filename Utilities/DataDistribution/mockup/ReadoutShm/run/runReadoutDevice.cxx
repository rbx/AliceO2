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
  options.add_options()
    (o2::DataDistribution::mockup::ReadoutDevice::OptionKeyOutputChannelName,
     bpo::value<std::string>()->default_value("readout"), "Name of the readout output channel")
    (o2::DataDistribution::mockup::ReadoutDevice::OptionKeyFreeShmChannelName,
     bpo::value<std::string>()->default_value("free-shm"), "Name of the free shm chunks channel")
    (o2::DataDistribution::mockup::ReadoutDevice::OptionKeyReadoutDataRegionSize,
     bpo::value<std::size_t>()->default_value(1ULL << 30 /* 1GiB */), "Size of the data shm segment")
    (o2::DataDistribution::mockup::ReadoutDevice::OptionKeyReadoutDescRegionSize,
     bpo::value<std::size_t>()->default_value(1ULL << 26 /* 64MiB */), "Size of the desc shm segment")
    (o2::DataDistribution::mockup::ReadoutDevice::OptionKeyReadoutSuperpageSize,
     bpo::value<size_t>()->default_value(1ULL << 20 /* 1MiB */), "CRU superpage size");
}

FairMQDevicePtr getDevice(const FairMQProgOptions & /*config*/) {
  return new o2::DataDistribution::mockup::ReadoutDevice();
}
