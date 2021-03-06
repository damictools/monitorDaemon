#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#include<sys/socket.h>  //socket
#include<arpa/inet.h>   //inet_addr

#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>
#include <sys/file.h>

using namespace std;

const int kMaxLine   = 2048;
const int kErrorCode =-1000;
const int kNANCode   =-1001;
const int kEmptyCode =-1002;
const int kNotReadingCode =-2001;
const int kCommErrorMask  = 10000;

const char kEmailAddress[] = "javiert@fnal.gov";
const char inetAddr[] = "127.0.0.1";
//const char inetAddr[] = "131.225.90.254"; //whirl
//const char inetAddr[] = "131.225.90.5"; //cyclone
const int portTemp = 2055;
const int portSetT = 2060;
const int portPres = 2050;
const int portPan  = 5355;
const long kMinTimeCryoChange = 240; //now 4 minutes, was 10 minutes
const long kLogTimeInterval = 15; //in seconds
const long kNewLogFileInterval = 20000; //in seconds
const float kMinSafePres = 4e-4;

// time_t lastCryoChange(0);
// bool gCryoStatus;

struct systemStatus_t{
  
  string logDir;
  
  bool inEmergencyState;
  
  bool readingImage;
  bool exposingImage;
  int intW;
  string panStatus;
  int imNum;
  int cryoStatRd;
  
  int cryoStatus;
  time_t lastCryoChange;
  int relay;
  
  time_t expoStart;
  time_t expoStop;
  
  time_t readStart;
  time_t readStop;
  
  float usrSetTemp;
  float setTemp;
  float temp;
  float tempExpoMax;
  float tempExpoMin;

  int   htrMode;  
  float htr;
  float htrExpoMax;
  float htrExpoMin;
  
  
  float pres;
  float presExpoMax;
  float presExpoMin;
  
  vector<float>  vTel;
  vector<string> vTelName;
  vector<string> vTelComm;
  vector<float>  vTelExpoMax;
  vector<float>  vTelExpoMin;
  
  systemStatus_t(): logDir(""),
                    inEmergencyState(false),
                    readingImage(false),exposingImage(false),intW(kEmptyCode),panStatus("UNKNOWN"),imNum(kEmptyCode),cryoStatRd(kEmptyCode),
                    cryoStatus(kEmptyCode),lastCryoChange(kEmptyCode),relay(kEmptyCode),
                    expoStart(kEmptyCode),expoStop(kEmptyCode),readStart(kEmptyCode),readStop(kEmptyCode),
                    usrSetTemp(kEmptyCode),setTemp(kEmptyCode),temp(kEmptyCode),tempExpoMax(kEmptyCode),tempExpoMin(kEmptyCode),
                    htrMode(kEmptyCode),htr(kEmptyCode),htrExpoMax(kEmptyCode),htrExpoMin(kEmptyCode),
                    pres(kEmptyCode),presExpoMax(kEmptyCode),presExpoMin(kEmptyCode),
                    vTel(0),vTelName(0),vTelComm(0),vTelExpoMax(0),vTelExpoMin(0){;};
};

systemStatus_t gSystemStatus;



bool fileExist(const char *fileName){
  ifstream in(fileName,ios::in);
  if(in.fail()){
    //cout <<"\nError reading file: " << fileName <<"\nThe file doesn't exist!\n\n";
    in.close();
    return false;
  }
  
  in.close();
  return true;
}

int talkToSocket(const char* inetAddr, const int port, const string &msj, string &response)
{
  response.clear();
  int sock;
  struct sockaddr_in server;
  char server_reply[2000]="";
  
  //Create socket
  sock = socket(AF_INET , SOCK_STREAM , 0);
  if (sock == -1){
    close(sock);
    return -2; 
  }
  
  struct timeval timeout;      
  timeout.tv_sec = 10;
  timeout.tv_usec = 0;

  /* socket timeout */
  setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
  setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
  
  server.sin_addr.s_addr = inet_addr(inetAddr);
  server.sin_family = AF_INET;
  server.sin_port = htons( port );
 
  //Connect to remote server
  if (connect(sock , (struct sockaddr *)&server , sizeof(server)) < 0){
    close(sock);
    return -3;
  }
  
  //Send some data
  if( send(sock , msj.c_str(), msj.size(), 0) < 0){
    close(sock);
    return -4;
  }

  //Receive a reply from the server
  if( recv(sock , server_reply , 2000 , 0) < 0){
    close(sock);
    return -5;
  }
  response = server_reply;
  
  close(sock);
  return 0;
}


string getStringData(const char* msj, const int sPort){

  string responseString("");
  int comCode = talkToSocket(inetAddr, sPort, msj, responseString);
  if ( comCode < 0){
    ostringstream oss;
    oss << comCode*kCommErrorMask;
    responseString = oss.str();
  }
  return responseString;
}


float getSensorData(const char* msj, const int sPort){
  
  string responseString = getStringData(msj, sPort);
  istringstream responseISS(responseString);
  
  float responseVal = kEmptyCode;
  responseISS >> responseVal;
  if (responseISS.fail()) return  kNANCode;  // not a number
  return responseVal; 
  
}


void getLogData(){
 
  gSystemStatus.setTemp = getSensorData("stp", portTemp); /* get set temperature */ 
  gSystemStatus.temp    = getSensorData("rtd", portTemp); /* get temperature */
  gSystemStatus.relay   = (int)getSensorData("rly", portTemp); /* get cryocooler relay status */
  gSystemStatus.htrMode = (int)getSensorData("ht?", portTemp); /* get heater mode */
  gSystemStatus.htr     = getSensorData("htr", portTemp); /* get heater power */
  
  gSystemStatus.pres  = getSensorData("prs", portPres); /* get pressure */
 

  /* get cryocooler status during reading */
  if( gSystemStatus.readingImage == true ){
    if(gSystemStatus.relay>0) gSystemStatus.cryoStatRd = 1;
    else if(gSystemStatus.relay<0) gSystemStatus.cryoStatRd = gSystemStatus.relay;
  }

 
  /* get all the pan variables */
  if( gSystemStatus.readingImage == false ){
    {
      const string msjPan = "get INTEG_WIDTH";
      gSystemStatus.intW = getSensorData(msjPan.c_str(), portPan);
    }

    {
      const string msjPan = "get status";
      gSystemStatus.panStatus = getStringData(msjPan.c_str(), portPan);
    }
    
    if(gSystemStatus.exposingImage){
      const string msjPan = "get imnumber";
      gSystemStatus.imNum = getSensorData(msjPan.c_str(), portPan);
    }
    
  }

  for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p){
    if( gSystemStatus.readingImage == false ){
      const string msjPan = "get " + gSystemStatus.vTelComm[p];
      gSystemStatus.vTel[p] = getSensorData(msjPan.c_str(), portPan);
    }
    else
     gSystemStatus.vTel[p] = kNotReadingCode; 
  }
  
}

/* Overrides time check turns the cryo off,*/
/* the CCDs bias voltages and shuts down panview */
void emergencyOff(){

  gSystemStatus.inEmergencyState = true;
  
  /* turn the cryo off */
  string responseCryo="";
  const string msjCryo = "off";
  talkToSocket(inetAddr, portTemp, msjCryo, responseCryo);

  
  string responsePan="";
  {/* turn off the CCDs bias voltages */
    const string msjPan = "att set SL5_CCD12_VSUB_LIMIT 0";
    talkToSocket(inetAddr, portPan, msjPan, responsePan);
  }
  {
    const string msjPan = "att set SL6_CCD12_VSUB_LIMIT 0";
    talkToSocket(inetAddr, portPan, msjPan, responsePan);
  }
  
  {/* shut down panview */
    const string msjPan = "shutdown";
    talkToSocket(inetAddr, portPan, msjPan, responsePan);
  }
}

void turnCryoOnOff(bool cryoON,string &responseCryo){
  responseCryo="";
  time_t currentTime;
  time(&currentTime);
  double dif = difftime(currentTime,gSystemStatus.lastCryoChange);
  
  if(dif>kMinTimeCryoChange){
    time( &(gSystemStatus.lastCryoChange) );
    gSystemStatus.cryoStatus = cryoON;
    const string msjCryo = cryoON ? "on":"off";
    int comCode = talkToSocket(inetAddr, portTemp, msjCryo, responseCryo);
    responseCryo+="\n";
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responseCryo = oss.str();
    }
  }
  else{
    ostringstream oss;
    oss << "Cryocooler status changed too recently. You have to wait " << kMinTimeCryoChange - dif << " seconds.\n"; 
    responseCryo = oss.str();
  }
}

void turnHtrOnOff(bool htrON,string &responseHtr){
  
  responseHtr="";
  const string msjHtr = htrON ? "ht1":"ht0";
  int comCode = talkToSocket(inetAddr, portTemp, msjHtr, responseHtr);
  
  if ( comCode < 0){
    ostringstream oss;
    oss << comCode << endl;
    responseHtr = oss.str();
  }
  else{
    responseHtr= htrON ? "Heater ON\n":"Heater OFF\n";
  }
  
  gSystemStatus.htrMode = (int)getSensorData("ht?", portTemp); /* get heater mode status */
  gSystemStatus.htr     = getSensorData("htr", portTemp); /* get heater power status */
  
}


void initExpoStats(){
 
  getLogData(); 

  gSystemStatus.tempExpoMax = gSystemStatus.temp;
  gSystemStatus.tempExpoMin = gSystemStatus.temp;
  gSystemStatus.htrExpoMax = gSystemStatus.htr;
  gSystemStatus.htrExpoMin = gSystemStatus.htr;
  gSystemStatus.presExpoMax = gSystemStatus.pres;
  gSystemStatus.presExpoMin = gSystemStatus.pres;
  for(unsigned int i=0;i<gSystemStatus.vTel.size();++i){
    gSystemStatus.vTelExpoMax[i] = gSystemStatus.vTel[i];
    gSystemStatus.vTelExpoMin[i] = gSystemStatus.vTel[i];
  }
  
}

void updateExpoStats(){
  
  if(gSystemStatus.temp > -1000){
    if(gSystemStatus.tempExpoMax < gSystemStatus.temp)
      gSystemStatus.tempExpoMax = gSystemStatus.temp;
    if(gSystemStatus.tempExpoMin > gSystemStatus.temp || gSystemStatus.tempExpoMin < kErrorCode)
      gSystemStatus.tempExpoMin = gSystemStatus.temp;
  }

  if(gSystemStatus.pres > -1000){
    if(gSystemStatus.presExpoMax < gSystemStatus.pres)
      gSystemStatus.presExpoMax = gSystemStatus.pres;
    if(gSystemStatus.presExpoMin > gSystemStatus.pres || gSystemStatus.presExpoMin < kErrorCode)
      gSystemStatus.presExpoMin = gSystemStatus.pres;
  }
  
  if( gSystemStatus.readingImage == false ){
    
    if(gSystemStatus.htr > -1000){
      if(gSystemStatus.htrExpoMax < gSystemStatus.htr)
        gSystemStatus.htrExpoMax = gSystemStatus.htr;
      if(gSystemStatus.htrExpoMin > gSystemStatus.htr || gSystemStatus.htrExpoMin < kErrorCode)
        gSystemStatus.htrExpoMin = gSystemStatus.htr;
    }
    
    for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p){

      if(gSystemStatus.vTel[p] < -1000) continue;

      if(gSystemStatus.vTelExpoMax[p] < gSystemStatus.vTel[p])
        gSystemStatus.vTelExpoMax[p] = gSystemStatus.vTel[p];
      if(gSystemStatus.vTelExpoMin[p] > gSystemStatus.vTel[p] || gSystemStatus.vTelExpoMin[p] < kErrorCode)
        gSystemStatus.vTelExpoMin[p] = gSystemStatus.vTel[p];

    }
  }
}

void changeSetTemp(float newSetTemp, string &responseSetTemp){
  responseSetTemp="";
  gSystemStatus.usrSetTemp = newSetTemp;  
  
  ostringstream msjSetTep;
  
  msjSetTep << "setpt " << gSystemStatus.usrSetTemp;
  
  int comCode = talkToSocket(inetAddr, portSetT, msjSetTep.str(), responseSetTemp);
  
  if ( comCode < 0){
    ostringstream oss;
    oss << comCode << endl;
    responseSetTemp = oss.str();
  }
  else{
    istringstream respISS(responseSetTemp);
    string strOldSetT;
    string strNewSetT;
    respISS >> strOldSetT >> strNewSetT;
    responseSetTemp = "Set Temp change: " + strOldSetT + " -> " + strNewSetT + "\n";
  }
}

void changeSetTempCmd(const char* recvBuff, string &responseSetTemp){
  responseSetTemp="";

  istringstream buffISS(&(recvBuff[7]));
  float newSetTemp = kEmptyCode;
  buffISS >> newSetTemp;
  if (buffISS.fail()){// not a number
    responseSetTemp="Error: new set temperature is not a number.\n Will not change the set.\n";
    return;
  }
  else{
    changeSetTemp(newSetTemp,responseSetTemp);
    return;
  }
}


int listenForCommands(int &listenfd)
{
  time_t ticks;
  char recvBuff[1025]="";
  char sendBuff[1025]="";
  
  int client_sock = accept(listenfd, (struct sockaddr*)NULL, NULL); 
  int read_size = recv(client_sock , recvBuff , 1025 , 0);
  
  ticks = time(NULL);
  snprintf(sendBuff, sizeof(sendBuff), "# %.24s\r\n", ctime(&ticks));
  write(client_sock, sendBuff, strlen(sendBuff));
  
  if(strncmp (recvBuff, "stop", 4) == 0){
    string response("Stop: DAMIC daemon.\n");
    write(client_sock , response.c_str() , response.size());
    return -1;
  }
  else if(strncmp (recvBuff, "stat", 4) == 0){
    string response("Status: DAMIC daemon.\nUp and running!\n");
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "cryoON", 6) == 0){
    string response;
    if(gSystemStatus.inEmergencyState){
      response = "In emergency state. Will not turn the cryo on\n";
    }
    else{
      turnCryoOnOff(true,response);
    }
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "cryoOFF", 7) == 0){
    string response;
    turnCryoOnOff(false,response);
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "htrON", 5) == 0){
    string response;
    turnHtrOnOff(true,response);
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "htrOFF", 6) == 0){
    string response;
    turnHtrOnOff(false,response);
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "setTEMP", 7) == 0){
    string response;
    changeSetTempCmd(recvBuff,response);
    write(client_sock , response.c_str() , response.size());
  }
  
  else if(strncmp (recvBuff, "expoStarted", 11) == 0){
    string response="Logging of exposure statistics started.\n";
    time( &(gSystemStatus.expoStart) );
    gSystemStatus.exposingImage = true;
    initExpoStats();
    write(client_sock , response.c_str() , response.size());
  }
  
  else if(strncmp (recvBuff, "expoEnded", 9) == 0){
    string response="Logging of exposure statistics ended.\n";
    time( &(gSystemStatus.expoStop) );
    gSystemStatus.exposingImage = false;
    write(client_sock , response.c_str() , response.size());
    
    if(gSystemStatus.readingImage == true){
      response="Read ended: will resume pan logging.\n";
      time( &(gSystemStatus.readStop) );
      gSystemStatus.readingImage = false;
      write(client_sock , response.c_str() , response.size());
    }
      
  }
  
  else if(strncmp (recvBuff, "readStarted", 11) == 0){
    string response="Read started: will suspend pan logging.\n";
    time( &(gSystemStatus.readStart) );
    gSystemStatus.readingImage = true;
    gSystemStatus.relay   = (int)getSensorData("rly", portTemp); /* get cryocooler relay status */
    gSystemStatus.cryoStatRd = gSystemStatus.relay;
    write(client_sock , response.c_str() , response.size());
  }
  
  else if(strncmp (recvBuff, "readEnded", 9) == 0){
    string response="Read ended: will resume pan logging.\n";
    time( &(gSystemStatus.readStop) );
    gSystemStatus.readingImage = false;
    write(client_sock , response.c_str() , response.size());
  }

  else if(strncmp (recvBuff, "printLastExpoStats", 18) == 0){
    
    ostringstream statOSS; 
    
    if(gSystemStatus.exposingImage){
      statOSS << "STILL EXPOSING \n";
      time_t currentTime;
      time( &currentTime);
      double dif = difftime(currentTime,gSystemStatus.expoStart);
      statOSS << "EXPTIME  " <<  dif  << endl;
      statOSS << "EXPSTART " << gSystemStatus.expoStart << endl << endl;
    }
    else{
      double dif = difftime(gSystemStatus.expoStop,gSystemStatus.expoStart);
      statOSS << "EXPTIME  " << dif << endl;
      statOSS << "EXPSTART " << gSystemStatus.expoStart << endl;
      statOSS << "EXPSTOP  " << gSystemStatus.expoStop << endl;
    }
    
    if(gSystemStatus.readingImage){
      statOSS << "CURRENTLY READING \n";
      time_t currentTime;
      time( &currentTime);
      double dif = difftime(currentTime,gSystemStatus.readStart);
      statOSS << "RDTIME   " <<  dif  << endl;
      statOSS << "RDSTART  " << gSystemStatus.readStart << endl;
    }
    else{
      double dif = difftime(gSystemStatus.readStop,gSystemStatus.readStart);
      statOSS << "RDTIME   " << dif << endl;
      statOSS << "RDSTART  " << gSystemStatus.readStart << endl;
      statOSS << "RDSTOP   " << gSystemStatus.readStop << endl;
    }

    statOSS << "CRYOSTRD " << gSystemStatus.cryoStatRd << endl;
 
    statOSS << "RUNID   " << gSystemStatus.imNum << endl;
    
    statOSS << "INTW    " << gSystemStatus.intW << endl;
 
    statOSS << "TEMPMAX " << gSystemStatus.tempExpoMax << endl;
    statOSS << "TEMPMIN " << gSystemStatus.tempExpoMin << endl;
    
    statOSS << "HTRMAX  " << gSystemStatus.htrExpoMax << endl;
    statOSS << "HTRMIN  " << gSystemStatus.htrExpoMin << endl;
    
    statOSS << "PRESMAX " << gSystemStatus.presExpoMax << endl;
    statOSS << "PRESMIN " << gSystemStatus.presExpoMin << endl;
    
    for(unsigned int p=0;p<gSystemStatus.vTelName.size();++p){
      statOSS << gSystemStatus.vTelName[p]+"MAX " << gSystemStatus.vTelExpoMax[p] << endl;
      statOSS << gSystemStatus.vTelName[p]+"MIN " << gSystemStatus.vTelExpoMin[p] << endl;
    }
    
    write(client_sock , statOSS.str().c_str() , statOSS.str().size());
  }
  
  else if(strncmp (recvBuff, "printCurrentStatus", 18) == 0){
    
    ostringstream statOSS; 
    
    if(gSystemStatus.inEmergencyState){
      statOSS << "\nEMERGENCY STATE\n\n";
    }
    
    if(gSystemStatus.exposingImage){
      statOSS << "EXPOSING\n";
      time_t currentTime;
      time( &currentTime);
      double dif = difftime(currentTime,gSystemStatus.expoStart);
      statOSS << "EXPTIME " <<  dif  << endl << endl;
    }
    else{
      time_t currentTime;
      time( &currentTime);
      double dif = difftime(currentTime,gSystemStatus.expoStop);
      statOSS << "LAST EXPOSURE ENDED " << dif << " SECONDS AGO\n";
    }
    
    if(gSystemStatus.readingImage){
      statOSS << "READING\n";
      time_t currentTime;
      time( &currentTime);
      double dif = difftime(currentTime,gSystemStatus.readStart);
      statOSS << "RDTIME " <<  dif  << endl << endl;
    }

    if(gSystemStatus.cryoStatus<0)    
      statOSS << "CRYO    " << "UNK" << endl;
    else
      statOSS << "CRYO    " << (gSystemStatus.cryoStatus ? "ON":"OFF") << endl;

    if(gSystemStatus.relay<0)
      statOSS << "RLY     " << "UNK" << endl;
    else
      statOSS << "RLY     " << (gSystemStatus.relay ? "ON":"OFF") << endl;
    
    statOSS << "USRSETT " << gSystemStatus.usrSetTemp << endl;
    statOSS << "SETTEMP " << gSystemStatus.setTemp << endl;
    statOSS << "TEMP    " << gSystemStatus.temp << endl;
    statOSS << "HTRMODE " << gSystemStatus.htrMode << endl;
    statOSS << "HTRPOW  " << gSystemStatus.htr << endl;
    statOSS << "PRES    " << gSystemStatus.pres << endl;
   
    statOSS << "PANSTAT " << gSystemStatus.panStatus << endl;
    for(unsigned int p=0;p<gSystemStatus.vTelName.size();++p){
      statOSS << gSystemStatus.vTelName[p] << " " << gSystemStatus.vTel[p] << endl;
    }
    
    write(client_sock , statOSS.str().c_str() , statOSS.str().size());
  }
 
  else if(strncmp (recvBuff, "refresh", 7) == 0){

    ostringstream statOSS;
    statOSS << "Getting readings..\n";
    /* Get data from all the sources */
    getLogData();
    statOSS << "done\n";

    write(client_sock , statOSS.str().c_str() , statOSS.str().size());
  }  
    
  else if(strncmp (recvBuff, "help", 4) == 0){
    
    ostringstream statOSS; 
    statOSS << "Available commands:\n";
    statOSS << "stop, stat, cryoON, cryoOFF, htrON, htrOFF, setTEMP<temp>, printLastExpoStats, printCurrentStatus, expoStarted, expoEnded, readStarted, readEnded, refresh\n";
    
    write(client_sock , statOSS.str().c_str() , statOSS.str().size());
  }
  
  
  
  close(client_sock);
  usleep(10000);
  return 0;
}



bool readConfFile(const string &confFile){
  
  ifstream in(confFile.c_str());
  
  if(!fileExist(confFile.c_str())){
    cout << "\nError reading input file: " << confFile.c_str() <<"\nThe file doesn't exist!\n";
    return false;
  }
  
  string logDir="";
  
  while(!in.eof()){
    char headerLine[kMaxLine];
    in.getline(headerLine,kMaxLine);
    string headerS(headerLine);
    
    size_t found = headerS.find_first_not_of(" \t\v\r\n");
    if(found == string::npos) continue;
    else if( headerLine[found] == '#') continue;
    
    if( headerS.compare(found,12,"LOGGING_DIR:") ==0){
      size_t tPos = headerS.find(":");
      istringstream shutTimeISS( headerS.substr(tPos+1) );
      shutTimeISS >> logDir;
    }
    else{
      cout << "Error: unexpected content in configuration file.\n\n";
      return 1;
    }
  
    if(logDir != "") break; 
  }

  if(logDir == ""){
    cout << "Error: missing LOGGING_DIR in header.\n\n";
    in.close();
    return false;
  }

  gSystemStatus.logDir = logDir;
  
  while(!in.eof()){
    char line[kMaxLine];
    in.getline(line,kMaxLine);

    string lineS(line);
    size_t found = lineS.find_first_not_of(" \t\v\r\n");
    if(found == string::npos) continue;
    else if( line[found] == '#') continue;

    std::stringstream iss(line);
    string aux;
    int nCols=0;
    while (iss >> aux) ++nCols;
    if(nCols!=2){
      cout << "Error: more that 2 columns in configuration file.\n";
      in.close();
      return false;
    }
    iss.clear();
    iss.seekg(ios_base::beg);
    string varName="";
    string varComm="";
    iss >> varName >> varComm;
    
    gSystemStatus.vTelName.push_back(varName);
    gSystemStatus.vTelComm.push_back(varComm);
  }
  const unsigned int nVars = gSystemStatus.vTelName.size();
  gSystemStatus.vTel.resize(nVars,kEmptyCode);
  gSystemStatus.vTelExpoMax.resize(nVars,kEmptyCode);
  gSystemStatus.vTelExpoMin.resize(nVars,kEmptyCode);
  return true;
}


bool initDaemon(){
  /* Our process ID and Session ID */
  pid_t pid, sid;

  /* Fork off the parent process */
  pid = fork();
  if(pid < 0) return false;
  
  /* If we got a good PID, then
      we can exit the parent process. */
  if(pid > 0) {
    exit(EXIT_SUCCESS);
  }
          
  /* Create a new SID for the child process */
  sid = setsid();
  if(sid < 0) return false;

  /* Change the current working directory */
  if((chdir("/")) < 0) return false;

  /* Close out the standard file descriptors */
//   close(STDIN_FILENO);
//   close(STDOUT_FILENO);
//   close(STDERR_FILENO);
  return true;
}


int initServer(){
  /* Socket server initialization */
  int listenfd = 0;
  struct sockaddr_in serv_addr; 
//  listenfd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
  listenfd = socket(AF_INET, SOCK_STREAM, 0);
  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(8888);
  struct timeval timeout;
  /* socket timeout */      
  timeout.tv_sec = 0;
  timeout.tv_usec = 10;
  setsockopt (listenfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
  setsockopt (listenfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)); 
  bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 
  listen(listenfd, 10); 
  
  return listenfd;
}

void printHelp(){
  cout << "\nThis program monitors and comunicate with all the DAMIC components.\n";
  cout << "\n";
  cout << "Usage:\n";
  cout << "  monidae.exe -c <configuration file name> \n\n";
  cout << "For any problems or bugs contact Javier Tiffenberg <javiert@fnal.gov>\n\n";
}

int processCommandLineArgs(const int argc, char *argv[], string &configFile){
  
  if(argc == 1){
    printHelp();
    return 1;
  }
  
  bool configFileFlag = false;
  int opt=0;
  while ( (opt = getopt(argc, argv, "c:hH?")) != -1) {
    switch (opt) {
    case 'c':
      if(!configFileFlag){
        configFile = optarg;
        configFileFlag = true;
      }
      else{
        cerr << "\nError, can not set more than one stats file!\n\n";
        return 2;
      }
      break;
    case 'h':
    case 'H':
    default: /* '?' */
      printHelp();
      return 1;
    }
  }
  
  if(!configFileFlag){
    cerr << "\nConfig file name missing.\n\n";
    return 2;
  }
  
  if((argc-optind) != 0){
    cerr << "\nError, too many arguments!\n\n";
    return 2;
  }
  
  return 0;
}

void initNewLogFile(ofstream &logFile){
  
  if(logFile.is_open()){
    logFile.close();
  }
  
  time_t logFileIniTime;
  time( &logFileIniTime);
  struct tm *ptm = gmtime ( &logFileIniTime );
  
  ostringstream logFileNameOSS;

  logFileNameOSS << gSystemStatus.logDir << "/log_" 
                 << ptm->tm_year+1900 << setfill ('0')  << setw (2) << ptm->tm_mon +1 << setfill ('0')  << setw (2) << ptm->tm_mday
                 << setfill ('0') << setw (2) << ptm->tm_hour << setfill ('0')  << setw (2) << ptm->tm_min 
                 << ".txt";
  
  logFile.open(logFileNameOSS.str().c_str());
  
  logFile << "#Time\tTemp\tPressure\t";
  for(unsigned int p=0;p<gSystemStatus.vTelName.size();++p)
     logFile << gSystemStatus.vTelName[p] << "\t";
  logFile << "htrMode\thtrPow\trelay\t";
  logFile << "cryoStatus\treadingImage\texposingImage";
  logFile << endl;

}

void sendEmail(const string &subject, const string &body){
  
  ostringstream mailCmdOSS;
  mailCmdOSS << "echo \"" << body << "\" | mail -s \"" << subject << "\" " << kEmailAddress;
  FILE* file = popen(mailCmdOSS.str().c_str(), "r");
  pclose(file);
}


void runSafetyChecks(){
  /* set temp check */
  if(gSystemStatus.usrSetTemp > 0 && (gSystemStatus.usrSetTemp != gSystemStatus.setTemp) ){
    string responseSetTemp;
    changeSetTemp(gSystemStatus.usrSetTemp,responseSetTemp);
    sendEmail("[DAMIC_DAEMON] SET TEMP WARNING", "Temp set had to be corrected by the daemon.");
  }
  
  /* vacuum check */
  if(gSystemStatus.cryoStatus && gSystemStatus.pres > kMinSafePres){
    sendEmail("[DAMIC_DAEMON] VACUUM WARNING", "The pressure is to high. Will turn off the cryocooler, the CCDs bias and shut down pnaview.");
    emergencyOff();
  }
  
}


int main(int argc, char *argv[]) {
  
  string configFile="";
  int retCode = processCommandLineArgs(argc, argv, configFile);

  if(retCode!=0){
    cerr << "\nThe daemon has not been started.\n\n";
    return retCode;
  }

  bool allOk = readConfFile(configFile.c_str());
  if(!allOk){
    cerr << "\nThe daemon has not been started.\n\n";
    return 1;
  }
  
  const string lockFile    = gSystemStatus.logDir+"/lock";

  int pid_file = open(lockFile.c_str(), O_CREAT | O_RDWR, 0666);
  int rc = flock(pid_file, LOCK_EX | LOCK_NB);
  if(rc!=0) {
    if(EWOULDBLOCK == errno){  // another instance is running
      cout << "\nThe lock file is locked!\n";
      cout << "Is there another instance of the DAMIC daemon running?\n";
      cout << "Kill it before starting a new one.\n";
      cout << "No new instances will be launched.\n\n";
    }
    else{
      cout << "\nCould not create lock file!\n";
      cout << "Check the logging directory in the config file\n\n";
    }
    return 1; 
  }

  cout << endl << "Logging dir: " << gSystemStatus.logDir << endl << endl; 
  for(unsigned int i=0;i<gSystemStatus.vTelName.size();++i){
    cout << gSystemStatus.vTelName[i] << "\t" << gSystemStatus.vTelComm[i] << endl;
  }
  cout << "\n";
  
  cout << "Launching the daemon.\n\n";
  
  /* init daemon */
  bool initOK = initDaemon();
  if (!initOK) exit(EXIT_FAILURE);

  /* Socket server initialization */
  int listenfd = initServer();
  
  /* Global variables initialization */
  time( &(gSystemStatus.lastCryoChange) );
  gSystemStatus.relay = (int)getSensorData("rly", portTemp); /* get cryocooler relay status */
  gSystemStatus.cryoStatus = gSystemStatus.relay;

  /* Change the file mode mask */
  umask(0);
          
  /* Open any logs here */
  ofstream logFile;
  time_t lastLogFileIniTime;
  time( &lastLogFileIniTime);
  initNewLogFile(logFile);
  
  /* The Big Loop */
  int i=0;
 
  time_t lastLogTime = 0;
  
  while (true) {

    if(listenForCommands(listenfd) == -1){
      break;
    }
    usleep(500000); /* wait 1/2 second */

    time_t currentTime;
    time( &currentTime);
    
    /* create a new log file if enough time has elapsed*/
    double logTimeDif = difftime(currentTime,lastLogFileIniTime);
    if(logTimeDif > kNewLogFileInterval){
      time( &lastLogFileIniTime);
      initNewLogFile(logFile);
    }
    
    /* Logging time */
    double dif = difftime(currentTime,lastLogTime);
    if(dif < kLogTimeInterval) continue;
    
    /* Get data from all the sources */
    getLogData();
    
    /* Safety checks */
    runSafetyChecks();
    
    /* Write the log */
    logFile << time (NULL) << " " << gSystemStatus.temp << "\t" << gSystemStatus.pres;
    for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p)
      logFile << "\t" << gSystemStatus.vTel[p];
    logFile << "\t" << gSystemStatus.htrMode;
    logFile << "\t" << gSystemStatus.htr;
    logFile << "\t" << gSystemStatus.relay;
    logFile << "\t" << gSystemStatus.cryoStatus; 
    logFile << "\t" << gSystemStatus.readingImage;
    logFile << "\t" << gSystemStatus.exposingImage << endl;

    if(gSystemStatus.exposingImage)
      updateExpoStats();
    
    i++;
    time( &lastLogTime ); 
  }
  //cout << "after\n";
  logFile.close();
  exit(EXIT_SUCCESS);
}

