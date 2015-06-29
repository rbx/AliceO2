/**
 * runFLPSender.cxx
 *
 * @since 2013-04-23
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <iostream>

#include "boost/program_options.hpp"

#include "FairMQLogger.h"
#include "FairMQTransportFactoryZMQ.h"

#include "FLPSender.h"

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
  int sendDelay;

  string dataInputSocketType;
  int dataInputBufSize;
  string dataInputMethod;
  string dataInputAddress;
  int dataInputRateLogging;

  string hbInputSocketType;
  int hbInputBufSize;
  string hbInputMethod;
  string hbInputAddress;
  int hbInputRateLogging;

  vector<string> outputSocketType;
  vector<int> outputBufSize;
  vector<string> outputMethod;
  vector<string> outputAddress;
  vector<int> outputRateLogging;
} DeviceOptions_t;

inline bool parse_cmd_line(int _argc, char* _argv[], DeviceOptions* _options)
{
  if (_options == NULL) {
    throw runtime_error("Internal error: options' container is empty.");
  }

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
    ("test-mode", bpo::value<int>()->default_value(0), "Run in test mode")
    ("send-offset", bpo::value<int>()->default_value(0), "Offset for staggered sending")
    ("send-delay", bpo::value<int>()->default_value(8), "Delay for staggered sending")

    ("data-input-socket-type", bpo::value<string>()->required(), "Data input socket type: sub/pull")
    ("data-input-buff-size", bpo::value<int>()->required(), "Data input buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("data-input-method", bpo::value<string>()->required(), "Data input method: bind/connect")
    ("data-input-address", bpo::value<string>()->required(), "Data input address, e.g.: \"tcp://localhost:5555\"")
    ("data-input-rate-logging", bpo::value<int>()->required(), "Log data input rate on socket, 1/0")

    ("hb-input-socket-type", bpo::value<string>()->default_value("sub"), "Heartbeat input socket type: sub/pull")
    ("hb-input-buff-size", bpo::value<int>()->default_value(100), "Heartbeat input buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("hb-input-method", bpo::value<string>()->default_value("bind"), "Heartbeat input method: bind/connect")
    ("hb-input-address", bpo::value<string>()->required(), "Heartbeat input address, e.g.: \"tcp://localhost:5555\"")
    ("hb-input-rate-logging", bpo::value<int>()->default_value(0), "Log heartbeat input rate on socket, 1/0")

    ("output-socket-type", bpo::value<vector<string>>()->required(), "Output socket type: pub/push")
    ("output-buff-size", bpo::value<vector<int>>()->required(), "Output buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("output-method", bpo::value<vector<string>>()->required(), "Output method: bind/connect")
    ("output-address", bpo::value<vector<string>>()->required(), "Output address, e.g.: \"tcp://localhost:5555\"")
    ("output-rate-logging", bpo::value<vector<int>>()->required(), "Log output rate on socket, 1/0")

    ("help", "Print help messages");

  bpo::variables_map vm;
  bpo::store(bpo::parse_command_line(_argc, _argv, desc), vm);

  if (vm.count("help")) {
    LOG(INFO) << "FLP Sender" << endl << desc;
    return false;
  }

  bpo::notify(vm);

  if (vm.count("id"))                      { _options->id                        = vm["id"].as<string>(); }
  if (vm.count("flp-index"))               { _options->flpIndex                  = vm["flp-index"].as<int>(); }
  if (vm.count("event-size"))              { _options->eventSize                 = vm["event-size"].as<int>(); }
  if (vm.count("io-threads"))              { _options->ioThreads                 = vm["io-threads"].as<int>(); }

  if (vm.count("num-epns"))                { _options->numEPNs                   = vm["num-epns"].as<int>(); }
  if (vm.count("num-outputs")) {
    _options->numEPNs = vm["num-outputs"].as<int>();
    LOG(WARN) << "configured via num-outputs command line option, it is deprecated. Use num-epns instead.";
  }

  if (vm.count("heartbeat-timeout"))       { _options->heartbeatTimeoutInMs      = vm["heartbeat-timeout"].as<int>(); }
  if (vm.count("test-mode"))               { _options->testMode                  = vm["test-mode"].as<int>(); }
  if (vm.count("send-offset"))             { _options->sendOffset                = vm["send-offset"].as<int>(); }
  if (vm.count("send-delay"))              { _options->sendDelay                 = vm["send-delay"].as<int>(); }

  if (vm.count("data-input-socket-type"))  { _options->dataInputSocketType       = vm["data-input-socket-type"].as<string>(); }
  if (vm.count("data-input-buff-size"))    { _options->dataInputBufSize          = vm["data-input-buff-size"].as<int>(); }
  if (vm.count("data-input-method"))       { _options->dataInputMethod           = vm["data-input-method"].as<string>(); }
  if (vm.count("data-input-address"))      { _options->dataInputAddress          = vm["data-input-address"].as<string>(); }
  if (vm.count("data-input-rate-logging")) { _options->dataInputRateLogging      = vm["data-input-rate-logging"].as<int>(); }

  if (vm.count("hb-input-socket-type"))    { _options->hbInputSocketType         = vm["hb-input-socket-type"].as<string>(); }
  if (vm.count("hb-input-buff-size"))      { _options->hbInputBufSize            = vm["hb-input-buff-size"].as<int>(); }
  if (vm.count("hb-input-method"))         { _options->hbInputMethod             = vm["hb-input-method"].as<string>(); }
  if (vm.count("hb-input-address"))        { _options->hbInputAddress            = vm["hb-input-address"].as<string>(); }
  if (vm.count("hb-input-rate-logging"))   { _options->hbInputRateLogging        = vm["hb-input-rate-logging"].as<int>(); }

  if (vm.count("output-socket-type"))      { _options->outputSocketType          = vm["output-socket-type"].as<vector<string>>(); }
  if (vm.count("output-buff-size"))        { _options->outputBufSize             = vm["output-buff-size"].as<vector<int>>(); }
  if (vm.count("output-method"))           { _options->outputMethod              = vm["output-method"].as<vector<string>>(); }
  if (vm.count("output-address"))          { _options->outputAddress             = vm["output-address"].as<vector<string>>(); }
  if (vm.count("output-rate-logging"))     { _options->outputRateLogging         = vm["output-rate-logging"].as<vector<int>>(); }

  return true;
}

int main(int argc, char** argv)
{
  FLPSender flp;

  DeviceOptions_t options;
  try {
    if (!parse_cmd_line(argc, argv, &options)) {
      return 0;
    }
  } catch (const exception& e) {
    LOG(ERROR) << e.what();
    return 1;
  }

  if (options.numEPNs <= 0) {
    LOG(ERROR) << "Configured with 0 EPNs, exiting. Use --num-epns program option.";
    exit(EXIT_FAILURE);
  }

  LOG(INFO) << "FLP Sender, ID: " << options.id << " (PID: " << getpid() << ")";

  FairMQTransportFactory* transportFactory = new FairMQTransportFactoryZMQ();

  flp.SetTransport(transportFactory);

  flp.SetProperty(FLPSender::Id, options.id);
  flp.SetProperty(FLPSender::Index, options.flpIndex);
  flp.SetProperty(FLPSender::NumIoThreads, options.ioThreads);
  flp.SetProperty(FLPSender::EventSize, options.eventSize);
  flp.SetProperty(FLPSender::HeartbeatTimeoutInMs, options.heartbeatTimeoutInMs);
  flp.SetProperty(FLPSender::TestMode, options.testMode);
  flp.SetProperty(FLPSender::SendOffset, options.sendOffset);
  flp.SetProperty(FLPSender::SendDelay, options.sendDelay);

  // configure data input channel
  FairMQChannel dataInputChannel(options.dataInputSocketType, options.dataInputMethod, options.dataInputAddress);
  dataInputChannel.UpdateSndBufSize(options.dataInputBufSize);
  dataInputChannel.UpdateRcvBufSize(options.dataInputBufSize);
  dataInputChannel.UpdateRateLogging(options.dataInputRateLogging);
  flp.fChannels["data-in"].push_back(dataInputChannel);

  // configure heartbeat input channel
  FairMQChannel hbInputChannel(options.hbInputSocketType, options.hbInputMethod, options.hbInputAddress);
  hbInputChannel.UpdateSndBufSize(options.hbInputBufSize);
  hbInputChannel.UpdateRcvBufSize(options.hbInputBufSize);
  hbInputChannel.UpdateRateLogging(options.hbInputRateLogging);
  flp.fChannels["heartbeat-in"].push_back(hbInputChannel);

  // configure data output channels
  for (int i = 0; i < options.numEPNs; ++i) {
    FairMQChannel outputChannel(options.outputSocketType.at(i), options.outputMethod.at(i), options.outputAddress.at(i));
    outputChannel.UpdateSndBufSize(options.outputBufSize.at(i));
    outputChannel.UpdateRcvBufSize(options.outputBufSize.at(i));
    outputChannel.UpdateRateLogging(options.outputRateLogging.at(i));
    flp.fChannels["data-out"].push_back(outputChannel);
  }

  flp.ChangeState("INIT_DEVICE");
  flp.WaitForEndOfState("INIT_DEVICE");

  flp.ChangeState("INIT_TASK");
  flp.WaitForEndOfState("INIT_TASK");

  flp.ChangeState("RUN");
  flp.InteractiveStateLoop();

  return 0;
}
