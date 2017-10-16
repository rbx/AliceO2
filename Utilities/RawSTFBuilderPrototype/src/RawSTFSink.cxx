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
 * RawSTFSink.cxx
 *
 * @since 2014-10-10
 * @author A. Rybalchenko
 */

#include "RawSTFBuilderPrototype/RawSTFSink.h"

using namespace std;

RawSTFSink::RawSTFSink()
{
}

void RawSTFSink::Run()
{
    FairMQChannel& dataInChannel = fChannels.at("data").at(0);

    while (CheckCurrentState(RUNNING))
    {
        FairMQMessagePtr msg(dataInChannel.Transport()->CreateMessage());
        dataInChannel.Receive(msg);
    }
}

RawSTFSink::~RawSTFSink()
{
}
