/**
 * runFLPSender.cxx
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <iostream>
#include <csignal>
#include <mutex>
#include <condition_variable>
#include <map>
#include <string>

#include <boost/asio.hpp> // for DDS
#include "boost/program_options.hpp"

#include "FairMQLogger.h"
#include "FairMQTransportFactoryZMQ.h"
#include "FairMQTools.h"

#include "FLPSender.h"

#include "KeyValue.h" // DDS

using namespace std;
using namespace AliceO2::Devices;

typedef struct DeviceOptions
{
  string id;
  int flpIndex;
  int eventSize;
  int ioThreads;
  int numEPNs;
  int heartbeatTimeoutInMs;
  int testMode;
  int sendOffset;
  vector<string> inputSocketType;
  vector<int> inputBufSize;
  vector<string> inputMethod;
  // vector<string> inputAddress;
  vector<int> inputRateLogging;
  string outputSocketType;
  int outputBufSize;
  string outputMethod;
  // vector<string> outputAddress;
  int outputRateLogging;
} DeviceOptions_t;

inline bool parse_cmd_line(int _argc, char* _argv[], DeviceOptions* _options)
{
  if (_options == NULL)
    throw runtime_error("Internal error: options' container is empty.");

  namespace bpo = boost::program_options;
  bpo::options_description desc("Options");
  desc.add_options()
    ("id", bpo::value<string>()->required(), "Device ID")
    ("flp-index", bpo::value<int>()->default_value(0), "FLP Index (for debugging in test mode)")
    ("event-size", bpo::value<int>()->default_value(1000), "Event size in bytes")
    ("io-threads", bpo::value<int>()->default_value(1), "Number of I/O threads")
    ("num-inputs", bpo::value<int>()->default_value(2), "Number of FLP input sockets")
    ("num-outputs", bpo::value<int>(), "Number of FLP output sockets (DEPRECATED)") // deprecated
    ("num-epns", bpo::value<int>()->default_value(0), "Number of EPNs")
    ("heartbeat-timeout", bpo::value<int>()->default_value(20000), "Heartbeat timeout in milliseconds")
    ("test-mode", bpo::value<int>()->default_value(0),"Run in test mode")
    ("send-offset", bpo::value<int>()->default_value(0), "Offset for staggered sending")
    ("input-socket-type", bpo::value<vector<string>>()->required(), "Input socket type: sub/pull")
    ("input-buff-size", bpo::value<vector<int>>()->required(), "Input buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("input-method", bpo::value<vector<string>>()->required(), "Input method: bind/connect")
    // ("input-address", bpo::value<vector<string>>()->required(), "Input address, e.g.: \"tcp://localhost:5555\"")
    ("input-rate-logging", bpo::value<vector<int>>()->required(), "Log input rate on socket, 1/0")
    ("output-socket-type", bpo::value<string>()->required(), "Output socket type: pub/push")
    ("output-buff-size", bpo::value<int>()->required(), "Output buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("output-method", bpo::value<string>()->required(), "Output method: bind/connect")
    // ("output-address", bpo::value<vector<string>>()->required(), "Output address, e.g.: \"tcp://localhost:5555\"")
    ("output-rate-logging", bpo::value<int>()->required(), "Log output rate on socket, 1/0")
    ("help", "Print help messages");

  bpo::variables_map vm;
  bpo::store(bpo::parse_command_line(_argc, _argv, desc), vm);

  if (vm.count("help")) {
    LOG(INFO) << "FLP Sender" << endl << desc;
    return false;
  }

  bpo::notify(vm);

  if (vm.count("id"))                  { _options->id                   = vm["id"].as<string>(); }
  if (vm.count("flp-index"))           { _options->flpIndex             = vm["flp-index"].as<int>(); }
  if (vm.count("event-size"))          { _options->eventSize            = vm["event-size"].as<int>(); }
  if (vm.count("io-threads"))          { _options->ioThreads            = vm["io-threads"].as<int>(); }

  if (vm.count("num-epns"))            { _options->numEPNs              = vm["num-epns"].as<int>(); }
  if (vm.count("num-outputs")) {
    _options->numEPNs = vm["num-outputs"].as<int>();
    LOG(WARN) << "configured via num-outputs command line option, it is deprecated. Use num-epns instead.";
  }

  if (vm.count("heartbeat-timeout"))   { _options->heartbeatTimeoutInMs = vm["heartbeat-timeout"].as<int>(); }
  if (vm.count("test-mode"))           { _options->testMode             = vm["test-mode"].as<int>(); }
  if (vm.count("send-offset"))         { _options->sendOffset           = vm["send-offset"].as<int>(); }

  if (vm.count("input-socket-type"))   { _options->inputSocketType      = vm["input-socket-type"].as<vector<string>>(); }
  if (vm.count("input-buff-size"))     { _options->inputBufSize         = vm["input-buff-size"].as<vector<int>>(); }
  if (vm.count("input-method"))        { _options->inputMethod          = vm["input-method"].as<vector<string>>(); }
  // if (vm.count("input-address"))      { _options->inputAddress         = vm["input-address"].as<vector<string>>(); }
  if (vm.count("input-rate-logging"))  { _options->inputRateLogging     = vm["input-rate-logging"].as<vector<int>>(); }

  if (vm.count("output-socket-type"))  { _options->outputSocketType     = vm["output-socket-type"].as<string>(); }
  if (vm.count("output-buff-size"))    { _options->outputBufSize        = vm["output-buff-size"].as<int>(); }
  if (vm.count("output-method"))       { _options->outputMethod         = vm["output-method"].as<string>(); }
  // if (vm.count("output-address"))     { _options->outputAddress        = vm["output-address"].as<vector<string>>(); }
  if (vm.count("output-rate-logging")) { _options->outputRateLogging    = vm["output-rate-logging"].as<int>(); }

  return true;
}

int main(int argc, char** argv)
{
  FLPSender flp;
  flp.CatchSignals();

  DeviceOptions_t options;
  try {
    if (!parse_cmd_line(argc, argv, &options))
      return 0;
  } catch (exception& e) {
    LOG(ERROR) << e.what();
    return 1;
  }

  if (options.numEPNs <= 0) {
    LOG(ERROR) << "Configured with 0 EPNs, exiting. Use --num-epns program option.";
    exit(EXIT_FAILURE);
  }

  LOG(INFO) << "FLP Sender, ID: " << options.id << " (PID: " << getpid() << ")";

  map<string,string> IPs;
  FairMQ::tools::getHostIPs(IPs);

  stringstream ss;

  if (IPs.count("ib0")) {
    ss << "tcp://" << IPs["ib0"];
  } else if (IPs.count("eth0")) {
    ss << "tcp://" << IPs["eth0"];
  } else {
    LOG(ERROR) << "Could not find ib0 or eth0 interface";
    exit(EXIT_FAILURE);
  }

  LOG(INFO) << "Running on " << ss.str();

  ss << ":5655";

  string initialInputAddress  = ss.str();

  // DDS
  // Waiting for properties
  dds::key_value::CKeyValue ddsKeyValue;
  dds::key_value::CKeyValue::valuesMap_t values;

  // In test mode, retreive the output address of FLPSyncSampler to connect to
  if (options.testMode == 1) {
    mutex keyMutex;
    condition_variable keyCondition;

    ddsKeyValue.subscribe([&keyCondition](const string& /*_key*/, const string& /*_value*/) { keyCondition.notify_all(); });
    ddsKeyValue.getValues("FLPSyncSamplerOutputAddress", &values);
    while (values.empty()) {
      unique_lock<mutex> lock(keyMutex);
      keyCondition.wait_until(lock, chrono::system_clock::now() + chrono::milliseconds(1000));
      ddsKeyValue.getValues("FLPSyncSamplerOutputAddress", &values);
    }
  }

  FairMQTransportFactory* transportFactory = new FairMQTransportFactoryZMQ();

  flp.SetTransport(transportFactory);

  // configure device
  flp.SetProperty(FLPSender::Id, options.id);
  flp.SetProperty(FLPSender::Index, options.flpIndex);
  flp.SetProperty(FLPSender::NumIoThreads, options.ioThreads);
  flp.SetProperty(FLPSender::EventSize, options.eventSize);
  flp.SetProperty(FLPSender::HeartbeatTimeoutInMs, options.heartbeatTimeoutInMs);
  flp.SetProperty(FLPSender::TestMode, options.testMode);
  flp.SetProperty(FLPSender::SendOffset, options.sendOffset);

  FairMQChannel hbInputChannel;
  hbInputChannel.UpdateType(options.inputSocketType.at(0));
  hbInputChannel.UpdateMethod(options.inputMethod.at(0));
  hbInputChannel.UpdateSndBufSize(options.inputBufSize.at(0));
  hbInputChannel.UpdateRcvBufSize(options.inputBufSize.at(0));
  hbInputChannel.UpdateRateLogging(options.inputRateLogging.at(0));
  flp.fChannels["heartbeat-in"].push_back(hbInputChannel);

  FairMQChannel dataInputChannel;
  dataInputChannel.UpdateType(options.inputSocketType.at(1));
  dataInputChannel.UpdateMethod(options.inputMethod.at(1));
  dataInputChannel.UpdateSndBufSize(options.inputBufSize.at(1));
  dataInputChannel.UpdateRcvBufSize(options.inputBufSize.at(1));
  dataInputChannel.UpdateRateLogging(options.inputRateLogging.at(1));
  flp.fChannels["data-in"].push_back(dataInputChannel);

  flp.fChannels["heartbeat-in"].at(0).UpdateAddress(initialInputAddress); // heartbeats
  if (options.testMode == 1) {
    // In test mode, assign address that was received from the FLPSyncSampler via DDS.
    flp.fChannels["data-in"].at(0).UpdateAddress(values.begin()->second); // FLPSyncSampler signal
  } else {
    // In regular mode, assign placeholder address, that will be set when binding.
    flp.fChannels["data-in"].at(0).UpdateAddress(initialInputAddress); // data
  }

  // configure outputs
  for (int i = 0; i < options.numEPNs; ++i) {
    FairMQChannel outputChannel(options.outputSocketType, options.outputMethod, "");
    outputChannel.UpdateSndBufSize(options.outputBufSize);
    outputChannel.UpdateRcvBufSize(options.outputBufSize);
    outputChannel.UpdateRateLogging(options.outputRateLogging);
    flp.fChannels["data-out"].push_back(outputChannel);
  }

  flp.ChangeState("INIT_DEVICE");
  flp.WaitForInitialValidation();

  if (options.testMode == 0) {
    // In regular mode, advertise the bound data input address to the DDS.
    ddsKeyValue.putValue("FLPSenderInputAddress", flp.fChannels["data-in"].at(0).GetAddress());
  }

  ddsKeyValue.putValue("FLPSenderHeartbeatInputAddress", flp.fChannels["heartbeat-in"].at(0).GetAddress());

  dds::key_value::CKeyValue::valuesMap_t values2;

  // Receive the EPNReceiver input addresses from DDS.
  {
  mutex keyMutex;
  condition_variable keyCondition;

  ddsKeyValue.subscribe([&keyCondition](const string& /*_key*/, const string& /*_value*/) {keyCondition.notify_all();});
  ddsKeyValue.getValues("EPNReceiverInputAddress", &values2);
  while (values2.size() != options.numEPNs) {
    unique_lock<mutex> lock(keyMutex);
    keyCondition.wait_until(lock, chrono::system_clock::now() + chrono::milliseconds(1000));
    ddsKeyValue.getValues("EPNReceiverInputAddress", &values2);
  }
  }

  // Assign the received EPNReceiver input addresses to the device.
  dds::key_value::CKeyValue::valuesMap_t::const_iterator it_values2 = values2.begin();
  for (int i = 0; i < options.numEPNs; ++i) {
    flp.fChannels["data-out"].at(i).UpdateAddress(it_values2->second);
    it_values2++;
  }

  // TODO: sort the data channels

  flp.WaitForEndOfState("INIT_DEVICE");

  flp.ChangeState("INIT_TASK");
  flp.WaitForEndOfState("INIT_TASK");

  flp.ChangeState("RUN");
  flp.WaitForEndOfState("RUN");

  flp.ChangeState("RESET_TASK");
  flp.WaitForEndOfState("RESET_TASK");

  flp.ChangeState("RESET_DEVICE");
  flp.WaitForEndOfState("RESET_DEVICE");

  flp.ChangeState("END");

  return 0;
}
