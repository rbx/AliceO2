#include <bitset>
#include <stdint.h>
#include <unistd.h>
#include <vector>

/// Maximum number of EPNs
#define O2_MAXIMUM_EPN_ID 1500

/// Maximum number of FLPs
#define O2_MAXIMUM_FLP_ID 512


/// Type to store a set of EPN ids.
typedef std::bitset<O2_MAXIMUM_EPN_ID> BitsetOfEpn;

/// Type: identifier for time frames
typedef uint64_t TimeFrameId;

/// Type: identifier for EPN
typedef uint16_t EpnId;

/// Type: identifier for FLP
typedef uint16_t FlpId;


/// Type: status of EPN
typedef enum {EPN_STATUS_UNDEFINED, EPN_STATUS_UNAVAILABLE, EPN_STATUS_AVAILABLE} EpnStatus;


/// A class for the internal machinery of the EPN scheduler
class EpnSchedulerInternals;

class ScheduleMsg {
  public:
  int id;             // a number to identify the schedule
  TimeFrameId tfMin;   // minimum timeframe id for range valid (included)
  TimeFrameId tfMax;   // maximum timeframe id for range valid (excluded)
  BitsetOfEpn epnIds;  // list of EPN IDs to be used during this range
};


class FlpStats {
  public:
    FlpId flpId;                    // the FLP id
    int timestamp;                  // timestamp of the statistics
    TimeFrameId maxTimeframeId;     // latest timeframe available
    float timeframeRate;            // number of timeframes per second during last period
};


/// The class to access the EPN scheduler for FLPs and EPNs
class EpnScheduler {
  public:

    /// Constructor
    EpnScheduler();    
    
    /// Destructor
    ~EpnScheduler();
    
    
    /// Opens the connexion to scheduler server(s).
    /// \param serverName the scheduler access parameters: comma separated host:port pairs of zookeeper servers
    /// \return zero on success, an error code otherwise.
    int initConnexion(const char *serverName);
    

    /// Register an EPN with a given ID.
    /// The registration is valid while the class is instanciated, the Id will be released after zookeeper timeout once the instance disconnects. Registration will fail if Id already in use.
    /// \param id this EPN id
    /// \return zero on success, an error code otherwise.
    int registerEpn(EpnId id);

    /// Set the status of an EPN with a given ID.
    /// \param id EPN id
    /// \param status EPN status
    /// \return zero on success, an error code otherwise.
    int setEpnStatus(EpnId id, EpnStatus status);

    /// Get the status of an EPN with a given ID.
    /// \param id EPN id
    /// \param status EPN status (result by reference)
    /// \return zero on success, an error code otherwise.
    int getEpnStatus(EpnId id, EpnStatus &status);  

    /// Get the list of all currently registered EPNs.
    /// \param ids bitset holding the list of EPNs (result by reference)
    /// \return zero on success, an error code otherwise.
    int getAllEpns(BitsetOfEpn &ids);

    /// Get the list of all currently registered and available EPNs.
    /// \param ids bitset holding the list of EPNs (result by reference)
    /// \return zero on success, an error code otherwise.  
    int getAvailableEpns(BitsetOfEpn &ids);


    enum scheduleErrorCode {OK, RETRY, AHEAD};
    /// Get the ID of the EPN where to send a given timeframe
    /// \param timeframeId the ID of the timeframe
    /// \param epnId the ID of the EPN where timeframe should be sent (result by reference)
    /// \return zero on success, SCHED_RETRY if schedule not yet available for this TF, SCHED_SKIP if schedule is ahead of timeframe, and an error code otherwise.  
    int getEpnIdFromTimeframeId(TimeFrameId timeframeId, EpnId &epnId);

    /// Register an FLP with a given ID.
    /// The registration is valid while the class is instanciated, the Id will be released after zookeeper timeout once the instance disconnects. Registration will fail if Id already in use.
    /// \param id this FLP id
    /// \return zero on success, an error code otherwise.
    int registerFlp(FlpId id);

    /// Create the master schedule
    /// \return zero on success, an error code otherwise.    
    int registerMaster();

    /// Update the global schedule
    /// \param schedule the definition of the new schedule to be used
    /// \return zero on success, an error code otherwise.  
    int updateSchedule(ScheduleMsg schedule);


    /// Publish FLP statistics (to be called by FLPs)
    /// \param stat FLP statistics
    /// \return zero on success, an error code otherwise.  
    int publishFlpStats(FlpStats stat);

    /// Retrieve (and flush) FLP statistics (to be called by master)
    /// \param stats a vector of FLP statistics (result by reference)
    /// \return zero on success, an error code otherwise.  
    int getFlpStats(std::vector<FlpStats> &stats);


    /// Set verbosity level
    /// \param verbosity verbosity level 0 (normal) 1 (debug)
    /// \return zero on success, an error code otherwise.     
    int setVerbosity(int verbosity);
    
    private:
      EpnSchedulerInternals *mPrivate;
};
