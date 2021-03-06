//****************************************************************************
//* This file is free software: you can redistribute it and/or modify        *
//* it under the terms of the GNU General Public License as published by     *
//* the Free Software Foundation, either version 3 of the License, or	     *
//* (at your option) any later version.					     *
//*                                                                          *
//* Primary Authors: Matthias Richter <richterm@scieq.net>                   *
//*                                                                          *
//* The authors make no claims about the suitability of this software for    *
//* any purpose. It is provided "as is" without express or implied warranty. *
//****************************************************************************

//  @file   MessageFormat.cxx
//  @author Matthias Richter
//  @since  2014-12-11
//  @brief  Helper class for message format of ALICE HLT data blocks

#include "MessageFormat.h"
#include "HOMERFactory.h"
#include "AliHLTHOMERData.h"
#include "AliHLTHOMERWriter.h"
#include "AliHLTHOMERReader.h"

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>

using namespace AliceO2::AliceHLT;
using namespace ALICE::HLT;

// TODO: central logging to be implemented

MessageFormat::MessageFormat()
  : mBlockDescriptors()
  , mDataBuffer()
  , mMessages()
  , mpFactory(NULL)
  , mOutputMode(kOutputModeSequence)
  , mListEvtData()
{
}

MessageFormat::~MessageFormat()
{
  if (mpFactory) delete mpFactory;
  mpFactory = NULL;
}

void MessageFormat::clear()
{
  mBlockDescriptors.clear();
  mDataBuffer.clear();
  mMessages.clear();
  mListEvtData.clear();
}

int MessageFormat::addMessage(AliHLTUInt8_t* buffer, unsigned size)
{
  // add message
  // this will extract the block descriptors from the message
  // the descriptors refer to data in the original message buffer

  unsigned count = mBlockDescriptors.size();
  // the buffer might start with an event descriptor of type AliHLTComponentEventData
  // the first read attempt is assuming the descriptor and checking consistency
  // - buffer size at least size of AliHLTComponentEventData struct
  // - fStructSize member matches
  // - number of blocks matches fBlockCnt member
  unsigned position=0;
  AliHLTComponentEventData* evtData=reinterpret_cast<AliHLTComponentEventData*>(buffer+position);
  if (position+sizeof(AliHLTComponentEventData)<=size &&
      evtData->fStructSize==sizeof(AliHLTComponentEventData)) {
    position += sizeof(AliHLTComponentEventData);
  } else {
    // one of the criteria does not match -> no event descriptor
    evtData=NULL;
  }
  do {
    if (readBlockSequence(buffer+position, size-position, mBlockDescriptors) < 0 ||
	evtData!=NULL && ((mBlockDescriptors.size()-count) != evtData->fBlockCnt)) {
      // not in the format of a single block, check if its a HOMER block
      if (readHOMERFormat(buffer+position, size-position, mBlockDescriptors) < 0 ||
	  evtData!=NULL && ((mBlockDescriptors.size()-count) != evtData->fBlockCnt)) {
	// not in HOMER format either
	if (position>0) {
	  // try once more without the assumption of event data header
	  position=0;
	  evtData=NULL;
	  continue;
	}
	break;
      }
    }
  } while (0);

  int result=0;
  if (evtData && (result=insertEvtData(*evtData))<0) {
    // error in the event data header, probably headers of different events
    mBlockDescriptors.resize(count);
    return result;
  }

  return mBlockDescriptors.size() - count;
}

int MessageFormat::addMessages(const vector<BufferDesc_t>& list)
{
  // add list of messages
  int totalCount = 0;
  int i = 0;
  for (vector<BufferDesc_t>::const_iterator data = list.begin(); data != list.end(); data++, i++) {
    if (data->mSize > 0) {
      unsigned nofEventHeaders=mListEvtData.size();
      int result = addMessage(data->mP, data->mSize);
      if (result > 0)
        totalCount += result;
      else if (result == 0 && nofEventHeaders==mListEvtData.size()) {
        cerr << "warning: no valid data blocks in message " << i << endl;
      } else {
	// severe error in the data
	// TODO: think about downstream data handlling
	mBlockDescriptors.clear();
      }
    } else {
      cerr << "warning: ignoring message " << i << " with payload of size 0" << endl;
    }
  }
}

int MessageFormat::readBlockSequence(AliHLTUInt8_t* buffer, unsigned size,
                                     vector<AliHLTComponentBlockData>& descriptorList) const
{
  // read a sequence of blocks consisting of AliHLTComponentBlockData followed by payload
  // from a buffer
  if (buffer == NULL) return 0;
  unsigned position = 0;
  vector<AliHLTComponentBlockData> input;
  while (position + sizeof(AliHLTComponentBlockData) < size) {
    AliHLTComponentBlockData* p = reinterpret_cast<AliHLTComponentBlockData*>(buffer + position);
    if (p->fStructSize == 0 ||                         // no valid header
        p->fStructSize + position > size ||            // no space for the header
        p->fStructSize + p->fSize + position > size) { // no space for the payload
      // the buffer is only a valid sequence of data blocks if payload
      // of the last block exacly matches the buffer boundary
      // otherwize all blocks added until now are ignored
      return -ENODATA;
    }
    // insert a new block
    input.push_back(*p);
    position += p->fStructSize;
    if (p->fSize > 0) {
      input.back().fPtr = buffer + position;
      position += p->fSize;
    } else {
      // Note: also a valid block, payload is optional
      input.back().fPtr = NULL;
    }
    // offset always 0 for iput blocks
    input.back().fOffset = 0;
  }

  descriptorList.insert(descriptorList.end(), input.begin(), input.end());
  return input.size();
}

int MessageFormat::readHOMERFormat(AliHLTUInt8_t* buffer, unsigned size,
                                   vector<AliHLTComponentBlockData>& descriptorList) const
{
  // read message payload in HOMER format
  if (mpFactory == NULL) const_cast<MessageFormat*>(this)->mpFactory = new ALICE::HLT::HOMERFactory;
  if (buffer == NULL || mpFactory == NULL) return -EINVAL;
  unique_ptr<AliHLTHOMERReader> reader(mpFactory->OpenReaderBuffer(buffer, size));
  if (reader.get() == NULL) return -ENOMEM;

  unsigned nofBlocks = 0;
  if (reader->ReadNextEvent() == 0) {
    nofBlocks = reader->GetBlockCnt();
    for (unsigned i = 0; i < nofBlocks; i++) {
      AliHLTComponentBlockData block;
      memset(&block, 0, sizeof(AliHLTComponentBlockData));
      block.fStructSize = sizeof(AliHLTComponentBlockData);
      block.fDataType.fStructSize = sizeof(AliHLTComponentDataType);
      homer_uint64 id = byteSwap64(reader->GetBlockDataType(i));
      homer_uint32 origin = byteSwap32(reader->GetBlockDataOrigin(i));
      memcpy(&block.fDataType.fID, &id,
             sizeof(id) > kAliHLTComponentDataTypefIDsize ? kAliHLTComponentDataTypefIDsize : sizeof(id));
      memcpy(&block.fDataType.fOrigin, &origin, 
             sizeof(origin) > kAliHLTComponentDataTypefOriginSize ? kAliHLTComponentDataTypefOriginSize : sizeof(origin));
      block.fSpecification = reader->GetBlockDataSpec(i);
      block.fPtr = const_cast<void*>(reader->GetBlockData(i));
      block.fSize = reader->GetBlockDataLength(i);
      descriptorList.push_back(block);
    }
  }

  return nofBlocks;
}

vector<MessageFormat::BufferDesc_t> MessageFormat::createMessages(const AliHLTComponentBlockData* blocks,
                                                                  unsigned count, unsigned totalPayloadSize,
                                                                  const AliHLTComponentEventData& evtData)
{
  const AliHLTComponentBlockData* pOutputBlocks = blocks;
  AliHLTUInt32_t outputBlockCnt = count;
  mDataBuffer.clear();
  mMessages.clear();
  if (mOutputMode == kOutputModeHOMER) {
    AliHLTHOMERWriter* pWriter = createHOMERFormat(pOutputBlocks, outputBlockCnt);
    if (pWriter) {
      AliHLTUInt32_t position = mDataBuffer.size();
      AliHLTUInt32_t startPosition = position;
      AliHLTUInt32_t payloadSize = pWriter->GetTotalMemorySize();
      mDataBuffer.resize(position + payloadSize + sizeof(evtData));
      memcpy(&mDataBuffer[position], &evtData, sizeof(evtData));
      position+=sizeof(evtData);
      pWriter->Copy(&mDataBuffer[position], 0, 0, 0, 0);
      mpFactory->DeleteWriter(pWriter);
      mMessages.push_back(MessageFormat::BufferDesc_t(&mDataBuffer[startPosition], payloadSize));
    }
  } else if (mOutputMode == kOutputModeMultiPart || mOutputMode == kOutputModeSequence) {
    // the output blocks are assempled in the internal buffer, for each
    // block BlockData is added as header information, directly followed
    // by the block payload
    //
    // kOutputModeMultiPart:
    // multi part mode adds one buffer descriptor per output block
    // the devices decides what to do with the multiple descriptors, one
    // option is to send them in a multi-part message
    //
    // kOutputModeSequence:
    // sequence mode concatenates the output blocks in the internal
    // buffer. In contrast to multi part mode, only one buffer descriptor
    // for the complete sequence is handed over to device
    AliHLTUInt32_t position = mDataBuffer.size();
    AliHLTUInt32_t startPosition = position;
    mDataBuffer.resize(position + count * sizeof(AliHLTComponentBlockData) + totalPayloadSize + sizeof(evtData));
    memcpy(&mDataBuffer[position], &evtData, sizeof(evtData));
    position+=sizeof(evtData);
    for (unsigned bi = 0; bi < count; bi++) {
      const AliHLTComponentBlockData* pOutputBlock = pOutputBlocks + bi;
      // copy BlockData and payload
      AliHLTUInt8_t* pData = reinterpret_cast<AliHLTUInt8_t*>(pOutputBlock->fPtr);
      pData += pOutputBlock->fOffset;
      AliHLTComponentBlockData* bdTarget = reinterpret_cast<AliHLTComponentBlockData*>(&mDataBuffer[position]);
      memcpy(bdTarget, pOutputBlock, sizeof(AliHLTComponentBlockData));
      bdTarget->fOffset = 0;
      bdTarget->fPtr = NULL;
      position += sizeof(AliHLTComponentBlockData);
      memcpy(&mDataBuffer[position], pData, pOutputBlock->fSize);
      position += pOutputBlock->fSize;
      if (mOutputMode == kOutputModeMultiPart) {
        // send one descriptor per block back to device
        mMessages.push_back(MessageFormat::BufferDesc_t(&mDataBuffer[startPosition], position - startPosition));
        startPosition = position;
      }
    }
    if (mOutputMode == kOutputModeSequence || count==0) {
      // send one single descriptor for all concatenated blocks
      mMessages.push_back(MessageFormat::BufferDesc_t(&mDataBuffer[startPosition], position - startPosition));
    }
  } else {
    // invalid output mode
    cerr << "error ALICE::HLT::Component: invalid output mode " << mOutputMode << endl;
  }
  return mMessages;
}

AliHLTHOMERWriter* MessageFormat::createHOMERFormat(const AliHLTComponentBlockData* pOutputBlocks,
                                                    AliHLTUInt32_t outputBlockCnt) const
{
  // send data blocks in HOMER format in one message
  int iResult = 0;
  if (mpFactory == NULL) const_cast<MessageFormat*>(this)->mpFactory = new ALICE::HLT::HOMERFactory;
  if (!mpFactory) return NULL;
  unique_ptr<AliHLTHOMERWriter> writer(mpFactory->OpenWriter());
  if (writer.get() == NULL) return NULL;

  homer_uint64 homerHeader[kCount_64b_Words];
  HOMERBlockDescriptor homerDescriptor(homerHeader);

  const AliHLTComponentBlockData* pOutputBlock = pOutputBlocks;
  for (unsigned blockIndex = 0; blockIndex < outputBlockCnt; blockIndex++, pOutputBlock++) {
    if (pOutputBlock->fPtr == NULL && pOutputBlock->fSize > 0) {
      cerr << "warning: ignoring block " << blockIndex << " because of missing data pointer" << endl;
    }
    memset(homerHeader, 0, sizeof(homer_uint64) * kCount_64b_Words);
    homerDescriptor.Initialize();
    homer_uint64 id = 0;
    homer_uint64 origin = 0;
    memcpy(&id, pOutputBlock->fDataType.fID, sizeof(homer_uint64));
    memcpy(((AliHLTUInt8_t*)&origin) + sizeof(homer_uint32), pOutputBlock->fDataType.fOrigin, sizeof(homer_uint32));
    homerDescriptor.SetType(byteSwap64(id));
    homerDescriptor.SetSubType1(byteSwap64(origin));
    homerDescriptor.SetSubType2(pOutputBlock->fSpecification);
    homerDescriptor.SetBlockSize(pOutputBlock->fSize);
    writer->AddBlock(homerHeader, pOutputBlock->fPtr);
  }
  return writer.release();
}

int MessageFormat::insertEvtData(const AliHLTComponentEventData& evtData)
{
  // insert event header to list, sort by time, oldest first
  if (mListEvtData.size()==0) {
    mListEvtData.push_back(evtData);
  } else {
    vector<AliHLTComponentEventData>::iterator it=mListEvtData.begin();
    for (; it!=mListEvtData.end(); it++) {
      if ((it->fEventCreation_us*1e3 + it->fEventCreation_us/1e3)>
	  (evtData.fEventCreation_us*1e3 + evtData.fEventCreation_us/1e3)) {
	// found a younger element
	break;
      }
    }
    // TODO: simple logic at the moment, header is not inserted
    // if there is a mismatch, as the headers are inserted one by one, all
    // headers in the list have the same ID
    if (evtData.fEventID!=it->fEventID) {
      cerr << "Error: mismatch in event ID for event with timestamp "
	   << evtData.fEventCreation_us*1e3 + evtData.fEventCreation_us/1e3 << " ms"
	   << endl;
      return -1;
    }
    // insert before the younger element
    mListEvtData.insert(it, evtData);
  }
}

AliHLTUInt64_t MessageFormat::byteSwap64(AliHLTUInt64_t src) const
{
  // swap a 64 bit number
  return ((src & 0xFFULL) << 56) | 
    ((src & 0xFF00ULL) << 40) | 
    ((src & 0xFF0000ULL) << 24) | 
    ((src & 0xFF000000ULL) << 8) | 
    ((src & 0xFF00000000ULL) >> 8) | 
    ((src & 0xFF0000000000ULL) >> 24) | 
    ((src & 0xFF000000000000ULL) >>  40) | 
    ((src & 0xFF00000000000000ULL) >> 56);
}

AliHLTUInt32_t MessageFormat::byteSwap32(AliHLTUInt32_t src) const
{
  // swap a 32 bit number
  return ((src & 0xFFULL) << 24) | 
    ((src & 0xFF00ULL) << 8) | 
    ((src & 0xFF0000ULL) >> 8) | 
    ((src & 0xFF000000ULL) >> 24);
}
