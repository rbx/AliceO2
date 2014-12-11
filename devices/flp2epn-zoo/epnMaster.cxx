/* implementation of a dummy EPN
*/

#include "EpnScheduler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>


int main(int argc,char **argv) {

  const char* server="127.0.0.1:2181";  // zookeeper server(s)
  int refreshTime=5;  // update time interval
  int delayTime=2;    // delay before schedule becomes active after publication (i.e. how much in advance we should publish new schedule before previous one expires)
  int retryTime=3;    // delay in seconds when no EPNs are available for new schedule
  float maxRate=10;  // estimated maximum timeframe rate seen on FLPs
  
  for (int i=1;i<argc;++i) {
   if (!strcmp("-s",argv[i])) {
     ++i;
     if (i==argc) {printf ("server list missing\n"); return -1;}
     server=argv[i];
   } else if (!strcmp("-r",argv[i])) {
     ++i;
     if (i==argc) {printf ("refreshTime missing\n"); return -1;}
     refreshTime=atoi(argv[i]);
   } else if (!strcmp("-d",argv[i])) {
     ++i;
     if (i==argc) {printf ("delayTime missing\n"); return -1;}
     delayTime=atoi(argv[i]);
   } else if (!strcmp("-f",argv[i])) {
     ++i;
     if (i==argc) {printf ("frameRate missing\n"); return -1;}
     maxRate=atof(argv[i]);
   }  
  }

  printf("Starting EPN master\n");
  printf("Zookeeper server=%s\n",server);
  printf("Refresh time=%d seconds\n",refreshTime);
  printf("Delay time=%d seconds\n",delayTime);
  printf("Estimated max frame rate=%.2f fps\n",maxRate);
 
  EpnScheduler s;
  int err=0;
  
  s.setVerbosity(0);
  
  err=s.initConnexion(server);
  if (err) {
    printf("initConnexion() failed : error %d\n",err);
    return -1;
  }
  
  err=s.registerMaster();
  if (err) {
    printf("registerMaster() failed : error %d\n",err);
    return -1;  
  }
  
  BitsetOfEpn availableEpns;
  
  time_t lastScheduleTime=0;
  ScheduleMsg lastSchedule;  // previous schedule (if there was one)
  int scheduleId=0;
  
  TimeFrameId maxTFId=0;  // max timeframe ID published by FLPs
  time_t maxStatsTime=time(NULL);  // time of the statistics for maxTFId
  
  for(int j=0;;++j) {
    time_t now=time(0);
    char tbuf[26]="";
    ctime_r(&now,tbuf);
    tbuf[strlen(tbuf)-1]=0;

    std::vector<FlpStats> flpStats;
    err=s.getFlpStats(flpStats);
    
    //printf("%d stats received\n",(int)flpStats.size());
    for (auto it=flpStats.begin();it!=flpStats.end();++it) {
      if (it->timeframeRate>maxRate) {
        maxRate=it->timeframeRate;
      }
      if (it->maxTimeframeId>maxTFId) {
        maxTFId=it->maxTimeframeId;
        maxStatsTime=it->timestamp;
      }
      //printf("stats: %d %d %d\n",it->flpId,it->timestamp,(int)it->maxTimeframeId);
    }
    printf("maxRate=%.2f, maxTF=%d\n",maxRate,(int)maxTFId);

    
    float remainingTimeForCurrentSchedule=0;
    if (lastScheduleTime && (lastSchedule.tfMax>maxTFId)) {
      remainingTimeForCurrentSchedule=(lastSchedule.tfMax-maxTFId)/maxRate;
    }
    printf("estimated remaining time before exhausting current schedule: %.2f seconds\n",remainingTimeForCurrentSchedule);
    if (remainingTimeForCurrentSchedule<refreshTime+delayTime) {
               
      err=s.getAvailableEpns(availableEpns);
      if (err) {
        printf("getAvailableEpns() failed : error %d\n",err);
      } else {
        char *asctime_r(const struct tm *tm, char *buf);
        printf("Available EPNs @ %s:\n",tbuf);
        if (availableEpns.any()) {
          for (unsigned int i=0;i<availableEpns.size();++i) {
            if (availableEpns.test(i)) {
              printf("EPN #%d\n",i);
            }
          }      
        } else {
          printf("none\n");
          printf("retry in %d seconds\n",retryTime);
          sleep(retryTime);
          continue;
        }
      }

      ScheduleMsg newSchedule;

      if (lastScheduleTime==0) {
        newSchedule.tfMin=0;
      } else {
        newSchedule.tfMin=lastSchedule.tfMax;
      }

      int numberOfTimeframesPerSchedule=(refreshTime+delayTime)*maxRate;    // number of timeframes published at each schedule (should match the refresh time and data taking rate)
      if (maxTFId>lastSchedule.tfMax) {
        numberOfTimeframesPerSchedule+=(maxTFId-lastSchedule.tfMax)+(now-maxStatsTime)*maxRate; // add missing timeframes + rate * time interval since missing timeframe published
      }
      
      // todo: limit the number of available EPNs
      // if  numberOfTimeframesPerSchedule<number of available EPNs
      
      newSchedule.id=++scheduleId;
      newSchedule.tfMax=newSchedule.tfMin+numberOfTimeframesPerSchedule;
      newSchedule.epnIds=availableEpns;
      lastScheduleTime=now;
      lastSchedule=newSchedule;

      // publish a new schedule
      printf("publish new schedule: %d->%d\n",(int)newSchedule.tfMin,(int)newSchedule.tfMax);



      err=s.updateSchedule(newSchedule);
      if (err) {
        printf("updateSchedule() failed: error %d\n",err);
      }

    }

    sleep(refreshTime);
  }
    
  return 0;
}
