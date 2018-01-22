# Data Distribution


Data Distribution O2Devices implementing SubTimeFrame building and data distribution tasks.

## Demonstrator chain

The chain starts with a `ReadoutDevice` (currently emulated) which produces HBFrames. Next in chain is `SubTimeFrameBuilderDevice`, which collects HBFrames belonging to the same SubTimeFrames. The final device in the chain is `SubTimeFrameTransporterDevice`, which is responsible for transporting the data to the EPN (to be implemented) and free the resources of the transported STF (readout buffers, shared memory allocated in the local processing, etc).

## Interfaces and data model

Readout process(es) send collections of STFrames when they become available (e.g. when a CRU superpage is completely filled). The exchange is implemented in the `StfInputInterface` class. `SubTimeFrameBuilder` device uses an object of `SubTimeFrame` class to track all HBFrames of the same STF. When the STF is built, it's sent to the next device in the chain using objects implementing visitor pattern (e.g. a pair of `InterleavedHdrDataSerializer` and `InterleavedHdrDataDeserializer` objects). These objects enumerate all data and headers of the STF, collecting all FairMQ messages for sending. Since all STF data is allocated in SHM the STF moving process is zero-copy.

## Running with the Readout CRU emulator

Make sure the Readout module is loaded in the environment.
Run the chain with the `bin/startFLPReadoutDataDistEMU.sh` script.
O2Device channel configuration is in `readout-flp-chain-emu.json`. Configuration for the readout process is in `readout_emu.cfg`. For configuration options please refer to `[consumer-fmq-wp5]`, `[equipment-emulator-1]`, and `[equipment-emulator-2]` sections.
To enable testing with two CRU emulators, set the option `[equipment-emulator-2]::enabled` to `1`.


## Running the O2 HBF emulator (deprecated)

O2Device channel configuration can be found in `readout-flp-chain.json`. The current configuration uses shared memory channels for all devices on the FLPs. To run the chain, please refer to `startReadoutFLPChain.sh` script.
Options:
 - `STF_BLD_NO_INPUTS`: the number of CRU readout processes. Can be set to 1 or 2
 - `EQUIPMENT_CNT`: this parameter determines how many independent HBFrame streams is being produced per CRU.
 - `EQUIPMENT_BITS_PER_S`: average data rate of each DMA stream
 - `DATA_REGION_SIZE`: Size of Shared Memory region used by a single CRU (decrease for small-scale testing)
 - `SUPERPAGE_SIZE`: (CRU emulator internal) superpage size
 - `DMA_CHUNK_SIZE`: (CRU emulator internal) DMA engine native block size. When set to zero the CRU emulator always produces contiguous HBFrames.
