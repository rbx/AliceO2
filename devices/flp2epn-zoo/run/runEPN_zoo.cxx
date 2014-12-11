/**
 * runEPN_distributed.cxx
 *
 * @since 2013-01-21
 * @author D. Klein, A. Rybalchenko, M. Al-Turany, C. Kouzinopoulos
 */

#include <iostream>
#include <csignal>

#include "boost/program_options.hpp"

#include "FairMQLogger.h"
#include "EPNex.h"

#ifdef NANOMSG
#include "FairMQTransportFactoryNN.h"
#else
#include "FairMQTransportFactoryZMQ.h"
#endif

using namespace std;

using namespace AliceO2::Devices;

EPNex epn;

static void s_signal_handler (int signal)
{
  cout << endl << "Caught signal " << signal << endl;

  epn.ChangeState(EPNex::STOP);
  epn.ChangeState(EPNex::END);

  cout << "Shutdown complete. Bye!" << endl;
  exit(1);
}

static void s_catch_signals (void)
{
  struct sigaction action;
  action.sa_handler = s_signal_handler;
  action.sa_flags = 0;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGTERM, &action, NULL);
}

typedef struct DeviceOptions
{
  string id;
  int ioThreads;
  int numFLPs;
  string schedulerAddress;
  string inputSocketType;
  int inputBufSize;
  string inputMethod;
  string inputAddress;
  int logInputRate;
} DeviceOptions_t;

inline bool parse_cmd_line(int _argc, char* _argv[], DeviceOptions* _options)
{
  if (_options == NULL)
    throw std::runtime_error("Internal error: options' container is empty.");

  namespace bpo = boost::program_options;
  bpo::options_description desc("Options");
  desc.add_options()
    ("id", bpo::value<string>()->required(), "Device ID")
    ("io-threads", bpo::value<int>()->default_value(1), "Number of I/O threads")
    ("num-flps", bpo::value<int>()->required(), "Number of FLPs")
    ("scheduler-address", bpo::value<string>()->required(), "Address of EpnScheduler")
    ("input-socket-type", bpo::value<string>()->required(), "Input socket type: sub/pull")
    ("input-buff-size", bpo::value<int>()->required(), "Input buffer size in number of messages (ZeroMQ)/bytes(nanomsg)")
    ("input-method", bpo::value<string>()->required(), "Input method: bind/connect")
    ("input-address", bpo::value<string>()->required(), "Input address, e.g.: \"tcp://localhost:5555\"")
    ("log-input-rate", bpo::value<int>()->required(), "Log input rate on socket, 1/0")
    ("help", "Print help messages");

  bpo::variables_map vm;
  bpo::store(bpo::parse_command_line(_argc, _argv, desc), vm);

  if (vm.count("help")) {
    LOG(INFO) << "EPN" << endl << desc;
    return false;
  }

  bpo::notify(vm);

  if (vm.count("id")) {
    _options->id = vm["id"].as<string>();
  }

  if (vm.count("io-threads")) {
    _options->ioThreads = vm["io-threads"].as<int>();
  }

  if (vm.count("num-flps")) {
    _options->numFLPs = vm["num-flps"].as<int>();
  }

  if (vm.count("scheduler-address")) {
    _options->schedulerAddress = vm["scheduler-address"].as<string>();
  }

  if (vm.count("input-socket-type")) {
    _options->inputSocketType = vm["input-socket-type"].as<string>();
  }

  if (vm.count("input-buff-size")) {
    _options->inputBufSize = vm["input-buff-size"].as<int>();
  }

  if (vm.count("input-method")) {
    _options->inputMethod = vm["input-method"].as<string>();
  }

  if (vm.count("input-address")) {
    _options->inputAddress = vm["input-address"].as<string>();
  }

  if (vm.count("log-input-rate")) {
    _options->logInputRate = vm["log-input-rate"].as<int>();
  }

  return true;
}

int main(int argc, char** argv)
{
  s_catch_signals();

  DeviceOptions_t options;
  try
  {
    if (!parse_cmd_line(argc, argv, &options))
      return 0;
  }
  catch (exception& e)
  {
    LOG(ERROR) << e.what();
    return 1;
  }

  LOG(INFO) << "PID: " << getpid();

#ifdef NANOMSG
  FairMQTransportFactory* transportFactory = new FairMQTransportFactoryNN();
#else
  FairMQTransportFactory* transportFactory = new FairMQTransportFactoryZMQ();
#endif

  epn.SetTransport(transportFactory);

  epn.SetProperty(EPNex::Id, options.id);
  epn.SetProperty(EPNex::NumIoThreads, options.ioThreads);
  epn.SetProperty(EPNex::NumFLPs, options.numFLPs);
  epn.SetProperty(EPNex::SchedulerAddress, options.schedulerAddress);

  epn.SetProperty(EPNex::NumInputs, 1);
  epn.SetProperty(EPNex::NumOutputs, 0);

  epn.ChangeState(EPNex::INIT);

  epn.SetProperty(EPNex::InputSocketType, options.inputSocketType);
  epn.SetProperty(EPNex::InputRcvBufSize, options.inputBufSize);
  epn.SetProperty(EPNex::InputMethod, options.inputMethod);
  epn.SetProperty(EPNex::InputAddress, options.inputAddress);
  epn.SetProperty(EPNex::LogInputRate, options.logInputRate);

  epn.ChangeState(EPNex::SETOUTPUT);
  epn.ChangeState(EPNex::SETINPUT);
  epn.ChangeState(EPNex::RUN);

  // wait until the running thread has finished processing.
  boost::unique_lock<boost::mutex> lock(epn.fRunningMutex);
  while (!epn.fRunningFinished)
  {
    epn.fRunningCondition.wait(lock);
  }

  epn.ChangeState(EPNex::STOP);
  epn.ChangeState(EPNex::END);

  return 0;
}
