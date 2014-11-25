#include <zookeeper.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <iostream>
#include <string>
#include <bitset>
#include <deque>
#include <mutex>
#include <vector>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <sstream>

#include "EpnScheduler.h"

#define O2_EPN_SCHEDULER_MASTER_PATH "/epnMaster"
#define O2_EPN_SCHEDULER_STATS_PATH "/epnScheduleStats"

/*
/local/soft/zookeeper-3.4.5/bin> ./zkServer.sh start
/local/soft/zookeeper-3.4.5/bin> ./zkCli.sh 

ls /
 export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/local/soft/zookeeper-3.4.5/src/c/.libs


 g++ scheduler.cpp -I/local/soft/zookeeper-3.4.5/src/c/include -I/local/soft/zookeeper-3.4.5/src/c/generated -lzookeeper_mt -L/local/soft/zookeeper-3.4.5/src/c/.libs/
*/


static const char* zk_state2String(int state){
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

static const char* zk_type2String(int state){
  if (state == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (state == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (state == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (state == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (state == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (state == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}



/*******************************************************/

class EpnSchedulerInternals {
  friend class EpnScheduler;
  
public:
  EpnSchedulerInternals();
  ~EpnSchedulerInternals();

  int initConnexion(const char* cx);  // cx: zookeeper server host(s) = comma separated host:port pairs
  static void watcher(zhandle_t *zzh, int type, int state, const char *path,void* context);

  // for FLPs
  FlpId flpId;
  int updateScheduleFromMaster(); // get latest schedule from master
  static void watcherGetSchedule(zhandle_t *zzh, int type, int state, const char *path, void *context);
  char msgBuffer[O2_MAXIMUM_EPN_ID+3+2*10];
  int msgBufferSz;
  std::deque<ScheduleMsg> scheduleQueue;
  std::mutex scheduleQueueMutex;
  TimeFrameId maxTimeframeIdQueried;
  
  FlpStats lastFlpStats;


  ScheduleMsg currentSchedule;
  std::vector<EpnId> currentScheduleActiveEpns; // table of active EPNs for current Schedule
  int scheduleOffset; // at which index to start the round-robin distribution for current schedule
  
protected:
  zhandle_t *zk_h;  /// handle to zookeeper API
  int zk_ok;        /// flag to keep zookeeper connexion status

private:  
  clientid_t zk_id; /// zookeeper client id, when connected
  int zk_timeout;   /// timeout after which zookeper connexion is closed by the server
  
  int shutdownThisThing; /// flag set when need to close connexion
  char *clientNodePath;  /// zookeeper node path

  const char *masterPath; /// zookeper path to master schedule 
  int verbose; /// set 1 for verbose debug logs
  
  
};


EpnSchedulerInternals::EpnSchedulerInternals() {
  zk_h=0;
  zk_ok=0;
  
  bzero(&zk_id,sizeof(zk_id));
  zk_timeout=3000;
  shutdownThisThing=0;
  clientNodePath=NULL;
  masterPath=NULL;
  verbose=0;
  
  msgBuffer[0]=0;
  msgBufferSz=sizeof(msgBuffer);
  currentSchedule.id=-1;
  scheduleOffset=0;
  
  maxTimeframeIdQueried=0;
  lastFlpStats.timestamp=0;
  lastFlpStats.maxTimeframeId=0;
  lastFlpStats.timeframeRate=0;
}

EpnSchedulerInternals::~EpnSchedulerInternals() {
  if (clientNodePath!=NULL) {
    zoo_delete(zk_h, clientNodePath,-1);
    free(clientNodePath);
    clientNodePath=NULL;
  }
  
  if (masterPath!=NULL) {
    zoo_delete(zk_h, masterPath,-1);
    masterPath=NULL;
  }

  
  if (zk_h!=0) {
    zookeeper_close(zk_h);
  }
}

int EpnSchedulerInternals::initConnexion(const char* cx) {
  if (verbose) {
    zoo_set_debug_level(ZOO_LOG_LEVEL_INFO);
  } else {
    zoo_set_debug_level(ZOO_LOG_LEVEL_WARN);
  }
  
  zoo_deterministic_conn_order(1); // enable deterministic order

  zk_h = zookeeper_init(cx, watcher, zk_timeout, &zk_id, this, 0);
  if (!zk_h) {
      return errno;
  }
  //printf("zk_h=%d\n",this->zk_h);

  if (!zk_h) {printf("zookeeper_init() failed"); return -1;}

  /// wait for initial connexion acknowledge
  for (int i=0;i<50;i++) {
     usleep(100000);
     if (zk_ok) {break;}
  }
  if (!zk_ok) {
    return __LINE__;
  }
  return 0;
}

/// This is the zookeeper callback handling routing, to watch for zookeeper events
/// It is running in a separate thread
void EpnSchedulerInternals::watcher(zhandle_t *zzh, int type, int state, const char *path, void* context){
  if (context==NULL) {
    return;
  }
  EpnSchedulerInternals *sc;
  sc=(EpnSchedulerInternals *)context;
  
    if (sc->verbose) {
      fprintf(stderr, "Watcher %s state = %s", zk_type2String(type), zk_state2String(state));
      if (path && strlen(path) > 0) {
        fprintf(stderr, " for path %s", path);
      }
      fprintf(stderr, "\n");
    }

    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            const clientid_t *id = zoo_client_id(zzh);
            if (sc->verbose) {
              fprintf(stderr, "Got a new session id: 0x%llx\n",(long long)id->client_id);
            }
            sc->zk_ok=1;
            /*
            if (h->z_id.client_id == 0 || h->z_id.client_id != id->client_id) {
                h->z_id = *id;
                fprintf(stderr, "Got a new session id: 0x%llx\n",
                        (long long)h->z_id.client_id);            
            }
            h->z_ok=1;sc->zk_id = zoo_client_id(zzh);
            fprintf(stderr, "Got a new session id: 0x%llx\n",
                        (long long)sc->zk_id->client_id);
            
            if (myid.client_id == 0 || myid.client_id != id->client_id) {
                myid = *id;
                fprintf(stderr, "Got a new session id: 0x%llx\n",
                        (long long)myid.client_id);
                if (clientIdFile) {
                    FILE *fh = fopen(clientIdFile, "w");
                    if (!fh) {
                        perror(clientIdFile);
                    } else {
                        int rc = fwrite(&myid, sizeof(myid), 1, fh);
                        if (rc != sizeof(myid)) {
                            perror("writing client id");
                        }
                        fclose(fh);
                    }
                }
            }
            */

        } else if (state == ZOO_AUTH_FAILED_STATE) {
            if (sc->verbose) {
              fprintf(stderr, "Authentication failure. Shutting down...\n");
            }
            zookeeper_close(zzh);
            sc->shutdownThisThing=1;
            sc->zk_h=0;
            sc->zk_ok=0;
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            if (sc->verbose) {
              fprintf(stderr, "Session expired. Shutting down...\n");
            }
            zookeeper_close(zzh);
            sc->shutdownThisThing=1;
            sc->zk_h=0;
            sc->zk_ok=0;
        }
    }
}

EpnScheduler::EpnScheduler() {
  mPrivate=new EpnSchedulerInternals();
}

EpnScheduler::~EpnScheduler() {
  if (mPrivate!=NULL) {
    delete mPrivate;
  }
}


int EpnScheduler::initConnexion(const char *serverName) {
  return mPrivate->initConnexion(serverName);
}


int getZookeeperNodeFromEpnId(EpnId id, char *buffer, unsigned int bufferSize) {
  if (buffer==NULL) {return __LINE__;}
  if (bufferSize<1) {return __LINE__;}
  buffer[0]=0;
  if (id>=O2_MAXIMUM_EPN_ID) {return __LINE__;}
  if (bufferSize<=13) {return __LINE__;}
  snprintf(buffer,bufferSize,"/epn/epn%04u",(unsigned int)id);
  return 0;
}

int getZookeeperNodeFromFlpId(FlpId id, char *buffer, unsigned int bufferSize) {
  if (buffer==NULL) {return __LINE__;}
  if (bufferSize<1) {return __LINE__;}
  buffer[0]=0;
  if (id>=O2_MAXIMUM_FLP_ID) {return __LINE__;}
  if (bufferSize<=13) {return __LINE__;}
  snprintf(buffer,bufferSize,"/flp/flp%04u",(unsigned int)id);
  return 0;
}



int EpnScheduler::setEpnStatus(EpnId id, EpnStatus status) {

  int err=0;  
  char path[20];
  err=getZookeeperNodeFromEpnId(id,path,sizeof(path));
  if (err) return err;

  char buffer[10]="";
  snprintf(buffer,sizeof(buffer),"%d",(int)status);
  err=zoo_set(mPrivate->zk_h, path, buffer, strlen(buffer)+1, -1);
  if (err) {
    printf("zoo_set(%s)=%d %s\n",path,err,zerror(err));
    return -1;
  }                 
                  
  return 0;
}

int EpnScheduler::getEpnStatus(EpnId id, EpnStatus &status) {
  
  int err=0;  
  char path[20];
  err=getZookeeperNodeFromEpnId(id,path,sizeof(path));
  if (err) return err;

  char buffer[10]="";
  int sz=sizeof(buffer);
  err=zoo_get(mPrivate->zk_h, path, 0, buffer, &sz, NULL);
  if (err) {
    printf("zoo_get(%s)=%d %s\n",path,err,zerror(err));
    return __LINE__;
  }
  if (sz<0) {
    return __LINE__;
  }
  
  buffer[sz]=0;
  if (mPrivate->verbose) {
    printf("%s=%s\n",path,buffer);
  }

  status=(EpnStatus)atoi(buffer);
  return 0;
}


int EpnScheduler::registerEpn(EpnId id) {

  if (mPrivate->clientNodePath!=NULL) {return __LINE__;} // can register only once

  int err=0;  
  char path[20];
  err=getZookeeperNodeFromEpnId(id,path,sizeof(path));
  if (err) return err;
  
  int sz=strlen(path);
  mPrivate->clientNodePath=(char *)malloc(sz+1);
  if (mPrivate->clientNodePath==NULL) {return __LINE__;}
  strcpy(mPrivate->clientNodePath,path);

  err=zoo_create( mPrivate->zk_h,path,NULL,-1,&ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,0,0);
  if (err) {
    printf("zoo_create(%s)=%d %s\n",path,err,zerror(err));
    return __LINE__;
  }
 
  return 0;
}

int EpnScheduler::registerFlp(FlpId id) {

  if (mPrivate->clientNodePath!=NULL) {return __LINE__;} // can register only once

  int err=0;  
  char path[20];
  err=getZookeeperNodeFromFlpId(id,path,sizeof(path));
  if (err) return err;
  
  int sz=strlen(path);
  mPrivate->clientNodePath=(char *)malloc(sz+1);
  if (mPrivate->clientNodePath==NULL) {return __LINE__;}
  strcpy(mPrivate->clientNodePath,path);

  err=zoo_create( mPrivate->zk_h,path,NULL,-1,&ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,0,0);
  if (err) {
    printf("zoo_create(%s)=%d %s\n",path,err,zerror(err));
    return __LINE__;
  }
 
  mPrivate->flpId=id;
 
  // initiate the schedule callback
  mPrivate->updateScheduleFromMaster();
 
  return 0;
}


int EpnScheduler::getAvailableEpns(BitsetOfEpn &ids) {

  int err;
  struct String_vector vs;
  const char *path="/epn";
  ids.reset();
  
  err=zoo_get_children(mPrivate->zk_h, path, 0, &vs);
  if (err) {
    printf("zoo_get_children(%s)=%d %s\n",path,err,zerror(err));
    return -1;
  }
  
  for (int i=0;i<vs.count;i++) {
    int id=-1;
    if (mPrivate->verbose) {
      printf("found %s\n",vs.data[i]);
    }
    if (vs.data[i]==NULL) {
      return __LINE__;
    }
    if (sscanf(vs.data[i],"epn%d",&id)==1) {
      if ((id>=0)&&(id<O2_MAXIMUM_EPN_ID)) {
        EpnStatus status=EPN_STATUS_UNDEFINED;
        if (getEpnStatus((EpnId)id,status)==0) {
          if (status==EPN_STATUS_AVAILABLE) {
            ids.set(id);
          }
        }
      } else {
        if (mPrivate->verbose) {
          printf("EPN id %d out of range\n",id);
        }
        return __LINE__;
      }
    } else {
      if (mPrivate->verbose) {
        printf("Can not parse EPN name %s\n",vs.data[i]);
      }
      return __LINE__;
    }
  }  
  
  return 0;
}


int EpnScheduler::registerMaster(){
  int err=0;
  
  if (mPrivate->masterPath==NULL) {
    mPrivate->masterPath=O2_EPN_SCHEDULER_MASTER_PATH;
    err=zoo_create( mPrivate->zk_h,mPrivate->masterPath,NULL,-1,&ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL,0,0);
    if (err) {
      if (mPrivate->verbose) {
        printf("zoo_create(%s)=%d %s\n",mPrivate->masterPath,err,zerror(err));
      }
      mPrivate->masterPath=NULL;
      return __LINE__;   
    }

    // create the other nodes
    err=zoo_create( mPrivate->zk_h,"/epn",NULL,-1,&ZOO_OPEN_ACL_UNSAFE, 0,0,0);
    err=zoo_create( mPrivate->zk_h,"/flp",NULL,-1,&ZOO_OPEN_ACL_UNSAFE, 0,0,0);   
    err=zoo_create( mPrivate->zk_h,O2_EPN_SCHEDULER_STATS_PATH,NULL,-1,&ZOO_OPEN_ACL_UNSAFE, 0,0,0); 
    return 0;
  }
  return __LINE__;
}



int getScheduleMsgFromString(const char *msgString, ScheduleMsg &schedule) {
  char msg[O2_MAXIMUM_EPN_ID+3+2*10];
  strncpy(msg,msgString,sizeof(msg)-1);
  msg[sizeof(msg)]=0;
    
  char *ptr1=msg;
  char *ptr2=msg;
  const char *end=msg+sizeof(msg)-1;
  for(ptr2=ptr1;;++ptr2) {
    if (ptr2==end) return __LINE__;
    if (*ptr2==',') {
      *ptr2=0;
      schedule.id=atoi(ptr1);
      break;
    }
  }
  ptr1=ptr2+1;  for(ptr2=ptr1;;++ptr2) {
    if (ptr2==end) return __LINE__;
    if (*ptr2==',') {
      *ptr2=0;
      schedule.tfMin=atoi(ptr1);
      break;
    }
  }
  ptr1=ptr2+1;
  for(ptr2=ptr1;;++ptr2) {
    if (ptr2==end) return __LINE__;
    if (*ptr2==',') {
      *ptr2=0;
      schedule.tfMax=atoi(ptr1);
      break;
    }
  }
  ptr1=ptr2+1;
  int i=0;
  for(ptr2=ptr1;;++ptr2) {
    if (ptr2==end) return __LINE__;
    if (*ptr2==0) break;
    if (i>=O2_MAXIMUM_EPN_ID) {
      printf("id %d\n",i);
      return __LINE__;
    }
    if (*ptr2=='0') {
      schedule.epnIds[i]=0;
    } else if (*ptr2=='1') {
      schedule.epnIds[i]=1;
    } else {
      return __LINE__;
    }
    i++;
  }
  return 0;
}








int EpnScheduler::updateSchedule(ScheduleMsg schedule) {
  int err=0;
  if (mPrivate->masterPath==NULL) {
    return __LINE__;  // we are not registered as master, can not update
  }
  
  // format data: tmin,tmax,epn bitmask as a string (LSB first)
  char msg[O2_MAXIMUM_EPN_ID+3+2*10];
  snprintf(msg,sizeof(msg),"%d,%d,%d,",(int)schedule.id,(int)schedule.tfMin,(int)schedule.tfMax);
  unsigned int j;
  j=strlen(msg);
  for (int i=0;i<O2_MAXIMUM_EPN_ID;++i) {
    if (j>=(sizeof(msg)-1)) {break;}
    msg[j]='0'+schedule.epnIds.test(i);
    ++j;
  }
  msg[j]=0;
  
  if (mPrivate->verbose) {
    printf("master schedule update: %s\n",msg);
  }
  
  err=zoo_set(mPrivate->zk_h, mPrivate->masterPath, msg, j, -1);
  if (err) {
    if (mPrivate->verbose) {
      printf("zoo_set(%s)=%d %s\n",mPrivate->masterPath,err,zerror(err));
    }
    return __LINE__;
  }                 

  return 0;
}


// return index in EPN list from timeframe id, schedule range, list size, and offset
int getEpnIndexFromTimeframeId(TimeFrameId timeframeId, TimeFrameId tfMin, TimeFrameId tfMax, int numberOfEpnsActive, int offset) {
  return (timeframeId-tfMin+offset)%numberOfEpnsActive;
}







int getFlpStats(EpnScheduler s, std::vector<FlpStats> &flpStats);



int EpnScheduler::getEpnIdFromTimeframeId(TimeFrameId timeframeId, EpnId &epnId) {
  
  if (timeframeId>mPrivate->maxTimeframeIdQueried) {
    mPrivate->maxTimeframeIdQueried=timeframeId;
  }
  
  ScheduleMsg newSchedule;
  int err=0;
  int publishStats=0; // flag set to one to request FLP stats publication  

  if ((mPrivate->currentSchedule.id<0)||(timeframeId<mPrivate->currentSchedule.tfMin)||(timeframeId>mPrivate->currentSchedule.tfMax)) {
  
    // no valid schedule, check in queue if there is one
    
    mPrivate->scheduleQueueMutex.lock();
    for(;;) {
      if (mPrivate->scheduleQueue.size()==0) {
      
        // publish stats for EPN master
        // so that he knows we are waiting for new schedule        
        publishStats=1;  
        err=EpnScheduler::RETRY; // no schedule available yet
        break;
      }
      if (mPrivate->scheduleQueue.front().tfMin>timeframeId) {
        err=EpnScheduler::AHEAD; // no schedule available for this time frame, although we have the schedule for next ones
        break;
      }
      if (mPrivate->scheduleQueue.front().tfMax>timeframeId) {
        // matching timeframe range found, update current schedule
        newSchedule=mPrivate->scheduleQueue.front();
        mPrivate->scheduleQueue.pop_front();
        break;
      }
    }
    mPrivate->scheduleQueueMutex.unlock();
    
    if (!err) {

      EpnId lastEpn=-1;
      if (mPrivate->currentSchedule.id>=0) {
        // keep id of EPN for last timeframe in previous schedule
        int ix=getEpnIndexFromTimeframeId(
            mPrivate->currentSchedule.tfMax,
            mPrivate->currentSchedule.tfMin,
            mPrivate->currentSchedule.tfMax,
            mPrivate->currentScheduleActiveEpns.size(),
            mPrivate->scheduleOffset
           );
        lastEpn=mPrivate->currentScheduleActiveEpns[ix];
        //printf("last epn=%d\n",(int)lastEpn);
      }

      mPrivate->currentSchedule=newSchedule;

      // build array of active EPNs from bitmask
      unsigned int nActiveEpns=mPrivate->currentSchedule.epnIds.count();
      mPrivate->currentScheduleActiveEpns.resize(nActiveEpns);
      unsigned int j=0;
      int offset=-1;
      for (unsigned int i=0;i<newSchedule.epnIds.size();i++) {
         if (newSchedule.epnIds[i]) {
           mPrivate->currentScheduleActiveEpns[j]=i;

           // update offset to try having sequential EPN usage,
           // i.e. try to start with the 1st one with id bigger than EPN for last timeframe in previous schedule
           if ( (offset==-1) && ( (lastEpn==-1)||(i>=lastEpn) ) ) {
             offset=j;
           }

           ++j;
         }
      }
      if (mPrivate->currentScheduleActiveEpns.size()!=j) {
        // number of bit sets and array size should match
        return __LINE__;
      }

      if (offset!=-1) {
        mPrivate->scheduleOffset=offset;
      } else {
        mPrivate->scheduleOffset=0;
      }

    }
  }

  if (!err) {
    // publish stats each 25% interval in current schedule range
    if (timeframeId - (mPrivate->lastFlpStats.maxTimeframeId) > (mPrivate->currentSchedule.tfMax-mPrivate->currentSchedule.tfMin)/4) {
      publishStats=1;
    }

    // select element round-robin in current list of active EPNs, starting from offset.
     int ix=getEpnIndexFromTimeframeId(
            timeframeId,
            mPrivate->currentSchedule.tfMin,
            mPrivate->currentSchedule.tfMax,
            mPrivate->currentScheduleActiveEpns.size(),
            mPrivate->scheduleOffset
           );
    epnId=mPrivate->currentScheduleActiveEpns[ix];
  }
  
  if (publishStats) {            
      FlpStats flpStats;
      flpStats.flpId=mPrivate->flpId;
      flpStats.timestamp=(int)time(NULL);
      flpStats.maxTimeframeId=mPrivate->maxTimeframeIdQueried;
      
      if (flpStats.timestamp>mPrivate->lastFlpStats.timestamp) {
        publishFlpStats(flpStats);
        if (mPrivate->verbose) {
          //printf("publishing FLP stats\n");
        }
      }
  }
  
  return err;
}




int EpnSchedulerInternals::updateScheduleFromMaster() {
  int err;
   
  msgBufferSz=sizeof(msgBuffer);
  
  err=zoo_wget(zk_h, O2_EPN_SCHEDULER_MASTER_PATH, watcherGetSchedule, this, msgBuffer, &msgBufferSz, NULL);
  if (err) {
    printf("zoo_get(%s)=%d %s\n",O2_EPN_SCHEDULER_MASTER_PATH,err,zerror(err));
    return __LINE__;   
  }
  
  if (msgBufferSz!=-1) {
    msgBuffer[msgBufferSz]=0;
    ScheduleMsg schedule;
    err=getScheduleMsgFromString(msgBuffer,schedule);
    if (verbose) {
      //printf("master=%s\nerr=%d\n",msgBuffer,err);
    }
    if (err==0) {
      scheduleQueueMutex.lock();
      scheduleQueue.push_back(schedule);
      scheduleQueueMutex.unlock();
    }
  }
  
  return 0;
}

void EpnSchedulerInternals::watcherGetSchedule(zhandle_t *zzh, int type, int state, const char *path, void *context) {
  EpnSchedulerInternals *p=NULL;
  p=(EpnSchedulerInternals *)context;
  
  if (p->verbose) {
    printf("got event %d state %d\n",type,state);
  }

  if (type==ZOO_CHANGED_EVENT) {    
    p->updateScheduleFromMaster();
  }  
}




int EpnScheduler::setVerbosity(int verbosity) {
  mPrivate->verbose=verbosity;
  return 0;
}



using namespace std;
void test() {
  char buf[128];
  int err=0;
  err=getZookeeperNodeFromEpnId(0,buf,sizeof(buf));
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(-1,buf,sizeof(buf));
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(123,buf,sizeof(buf));
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(12345,buf,sizeof(buf));
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(123,buf,1);
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(123,NULL,1);
  printf("%d=> %s\n",err,buf);
  err=getZookeeperNodeFromEpnId(123,buf,0);
  printf("%d=> %s\n",err,buf);


}

/*
int getFlpStats(EpnScheduler s, std::vector<FlpStats> &flpStats){

  int err;
  struct String_vector vs;
  const char *zk_path=O2_EPN_SCHEDULER_STATS_PATH;
  
  err=zoo_get_children(s.mPrivate, zk_path, 0, &vs);
  if (err) {
    printf("zoo_get_children(%s)=%d %s\n",zk_path,err,zerror(err));
    return -1;
  }
  
  for (int i=0;i<vs.count;i++) {
    int id=-1;
    printf("found %s\n",vs.data[i]);

    if (vs.data[i]==NULL) {
      return __LINE__;
    }
  }
  
  return 0;
}
*/



class FlpStatsMsg {

  private:
    friend class boost::serialization::access;
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & stats.flpId;
        ar & stats.timestamp;
        ar & stats.maxTimeframeId;
        ar & stats.timeframeRate;
    }
  public:
    FlpStats stats;
};




int EpnScheduler::publishFlpStats(FlpStats stat) {
  
    if ((stat.timestamp!=mPrivate->lastFlpStats.timestamp)&&(mPrivate->lastFlpStats.timestamp!=0)) {
      stat.timeframeRate=(stat.maxTimeframeId-mPrivate->lastFlpStats.maxTimeframeId)*1.0/(stat.timestamp-mPrivate->lastFlpStats.timestamp);
    } else {
      stat.timeframeRate=0;
    }

    // printf("%d,%d ; %d,%d\n",(int)mPrivate->lastFlpStats.maxTimeframeId,(int)mPrivate->lastFlpStats.timestamp,(int)stat.maxTimeframeId,(int)stat.timestamp);
    // printf( "rate=%.2f\n",stat.timeframeRate);
  
    FlpStatsMsg msg;
    msg.stats=stat;
  
    std::stringstream ss; 
    boost::archive::text_oarchive oa(ss);
    oa << msg;
    const std::string svalue = ss.str();
    const char *value=svalue.c_str();
    int valueSz=strlen(value)+1;
    //printf("valueSz=%d\n",valueSz);
    //std::cout << "publish: " << value << endl;
    int errZoo=zoo_create( mPrivate->zk_h,O2_EPN_SCHEDULER_STATS_PATH "/maxTf-",value,valueSz,&ZOO_OPEN_ACL_UNSAFE, ZOO_EPHEMERAL+ZOO_SEQUENCE,0,0);
    if (errZoo) {
      printf("zoo_create(%s)=%d %s\n",O2_EPN_SCHEDULER_STATS_PATH,errZoo,zerror(errZoo));
    }

    mPrivate->lastFlpStats=stat;
    //std::cout << "publish: " << value << endl;
        
  return 0;
}


int EpnScheduler::getFlpStats(std::vector<FlpStats> &resultStats) {
  int err;
  struct String_vector vs;
  const char *zk_path=O2_EPN_SCHEDULER_STATS_PATH;
  
  err=zoo_get_children(mPrivate->zk_h, zk_path, 0, &vs);
  if (err) {
    printf("zoo_get_children(%s)=%d %s\n",zk_path,err,zerror(err));
    return -1;
  }
  
  resultStats.clear();
  
  for (int i=0;i<vs.count;i++) {
    //printf("found node %s\n",vs.data[i]);

    if (vs.data[i]==NULL) {
      return __LINE__;
    }
    
    char childnode[200];
    snprintf(childnode,sizeof(childnode),"%s/%s",zk_path,vs.data[i]);

    char buffer[200]="";
    int sz=sizeof(buffer);
    err=zoo_get(mPrivate->zk_h, childnode, 0, buffer, &sz, NULL);
    
    if (!err) {
      if (sz>=0) {
        buffer[sz]=0;

        FlpStatsMsg msg;

        std::stringstream ss;
        ss << buffer;
        //std::cout << "decoding:" << ss << endl;
        boost::archive::text_iarchive oa(ss);
        oa >> msg;

       printf("stats: %d %d %d\n",msg.stats.flpId,msg.stats.timestamp,(int)msg.stats.maxTimeframeId);
        
        resultStats.push_back(msg.stats);
      } else {
        printf("getFlpStats(): empty string\n");
      }

    } else {
      printf("zoo_get(%s)=%d %s\n",childnode,err,zerror(err));
    }
    
    err=zoo_delete(mPrivate->zk_h, childnode, -1);
    if (err) {
      printf("zoo_delete(%s)=%d %s\n",childnode,err,zerror(err));
    }
    
  }

  return 0;
}
