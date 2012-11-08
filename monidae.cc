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

#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <vector>
#include <sys/file.h>

using namespace std;

const int kMaxLine = 2048;


// const char inetAddr[] = "127.0.0.1";
const char inetAddr[] = "131.225.90.254"; //whirl
//const char inetAddr[] = "131.225.90.5"; //cyclone
const int portTemp = 2055;
const int portPres = 2050;
const int portPan  = 5355;
const long kMinTimeCryoChange = 600; //10 minutes
const long kLogTimeInterval = 3; //in seconds

// time_t lastCryoChange(0);
// bool gCryoStatus;

struct systemStatus_t{
  
  string logDir;
  
  bool readingImage;
  bool exposingImage;
  
  int cryoStatus;
  time_t lastCryoChange;
  
  time_t expoStart;
  time_t expoStop;
  
  float temp;
  float tempExpoMax;
  float tempExpoMin;
  
  float pres;
  float presExpoMax;
  float presExpoMin;
  
  vector<float>  vTel;
  vector<string> vTelName;
  vector<string> vTelComm;
  vector<float>  vTelExpoMax;
  vector<float>  vTelExpoMin;
  
  systemStatus_t(): logDir(""),
                    readingImage(false),exposingImage(false),
                    cryoStatus(-1),lastCryoChange(-1),
                    expoStart(-1),expoStop(-1),
                    temp(-1),tempExpoMax(-1),tempExpoMin(-1),
                    pres(-1),presExpoMax(-1),presExpoMin(-1),
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
  timeout.tv_sec = 5;
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

void initExpoStats(){
  
  gSystemStatus.tempExpoMax = gSystemStatus.temp;
  gSystemStatus.tempExpoMin = gSystemStatus.temp;
  gSystemStatus.presExpoMax = gSystemStatus.pres;
  gSystemStatus.presExpoMin = gSystemStatus.pres;
  for(unsigned int i=0;i<gSystemStatus.vTel.size();++i){
    gSystemStatus.vTelExpoMax[i] = gSystemStatus.vTel[i];
    gSystemStatus.vTelExpoMin[i] = gSystemStatus.vTel[i];
  }
  
}

void updateExpoStats(){
  if(gSystemStatus.tempExpoMax < gSystemStatus.temp)
    gSystemStatus.tempExpoMax = gSystemStatus.temp;
  if(gSystemStatus.tempExpoMin > gSystemStatus.temp)
    gSystemStatus.tempExpoMin = gSystemStatus.temp;

  if(gSystemStatus.presExpoMax < gSystemStatus.pres)
    gSystemStatus.presExpoMax = gSystemStatus.pres;
  if(gSystemStatus.presExpoMin > gSystemStatus.pres)
    gSystemStatus.presExpoMin = gSystemStatus.pres;

  if( !(gSystemStatus.readingImage) ){
    for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p){
      if(gSystemStatus.vTel[p] == -2000) continue;
      if(gSystemStatus.vTelExpoMax[p] < gSystemStatus.vTel[p])
        gSystemStatus.vTelExpoMax[p] = gSystemStatus.vTel[p];
      if(gSystemStatus.vTelExpoMin[p] > gSystemStatus.vTel[p])
        gSystemStatus.vTelExpoMin[p] = gSystemStatus.vTel[p];
    }
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
  snprintf(sendBuff, sizeof(sendBuff), "%.24s\r\n", ctime(&ticks));
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
    turnCryoOnOff(true,response);
    write(client_sock , response.c_str() , response.size());
  }
  else if(strncmp (recvBuff, "cryoOFF", 7) == 0){
    string response;
    turnCryoOnOff(false,response);
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
      gSystemStatus.readingImage = false;
      write(client_sock , response.c_str() , response.size());
    }
      
  }
  
  else if(strncmp (recvBuff, "readStarted", 11) == 0){
    string response="Read started: will suspend pan logging.\n";
    gSystemStatus.readingImage = true;
    write(client_sock , response.c_str() , response.size());
  }
  
  else if(strncmp (recvBuff, "readEnded", 9) == 0){
    string response="Read ended: will resume pan logging.\n";
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
      statOSS << "EXPTIME " <<  dif  << endl << endl;
    }
    else{
      double dif = difftime(gSystemStatus.expoStop,gSystemStatus.expoStart);
      statOSS << "EXPTIME " << dif << endl;
    }
    
    statOSS << "TEMPMAX " << gSystemStatus.tempExpoMax << endl;
    statOSS << "TEMPMIN " << gSystemStatus.tempExpoMin << endl;
    
    statOSS << "PRESMAX " << gSystemStatus.presExpoMax << endl;
    statOSS << "PRESMIN " << gSystemStatus.presExpoMin << endl;
    
    for(unsigned int p=0;p<gSystemStatus.vTelName.size();++p){
      statOSS << gSystemStatus.vTelName[p]+"MAX " << gSystemStatus.vTelExpoMax[p] << endl;
      statOSS << gSystemStatus.vTelName[p]+"MIN " << gSystemStatus.vTelExpoMin[p] << endl;
    }
    
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
    
    unsigned int found = headerS.find_first_not_of(" \t\v\r\n");
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
    unsigned int found = lineS.find_first_not_of(" \t\v\r\n");
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
  gSystemStatus.vTel.resize(nVars,-1000);
  gSystemStatus.vTelExpoMax.resize(nVars,-1000);
  gSystemStatus.vTelExpoMin.resize(nVars,-1000);
  return true;
}


void getLogData(){
  /* get temperature */
  {
    const string msjTemp("rtd");
    string responseTemp("");
    int comCode = talkToSocket(inetAddr, portTemp, msjTemp, responseTemp);
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responseTemp = oss.str();
    }
    istringstream tempISS(responseTemp);
    float tempAux = -1;
    tempISS >> tempAux;
    if (tempISS.fail()) gSystemStatus.temp = -1;  // not a number
    else gSystemStatus.temp = tempAux; 
  }
  
  /* get presure */
  {
    const string msjPres("prs");
    string responsePres("");
    int comCode = talkToSocket(inetAddr, portPres, msjPres, responsePres);
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responsePres = oss.str();
    }
    istringstream presISS(responsePres);
    float presAux = -1;
    presISS >> presAux;
    if (presISS.fail()) gSystemStatus.pres = -1;  // not a number
    else gSystemStatus.pres = presAux; 
  }
  
  /* get all the pan variables */
  
  {
    for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p){
      string responsePan("");
      if( !(gSystemStatus.readingImage) ){
        const string msjPan = "get " + gSystemStatus.vTelComm[p];
        int comCode = talkToSocket(inetAddr, portPan, msjPan, responsePan);
        if ( comCode < 0){
          ostringstream oss;
          oss << comCode;
          responsePan = oss.str();
        }
      }
      else responsePan = "-2000";
      
      istringstream panISS(responsePan);
      float panAux = -1;
      panISS >> panAux;
      if (panISS.fail()) gSystemStatus.vTel[p] = -1000;  // not a number
      else gSystemStatus.vTel[p] = panAux;
    }
  }
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


int main(void) {
  
  readConfFile("monidae.conf");
  
  const string logFileName = gSystemStatus.logDir+"/log.txt";
  const string lockFile    = gSystemStatus.logDir+"/lock";

  cout << lockFile << endl;
  
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
  
  /* init daemon */
  bool initOK = initDaemon();
  if (!initOK) exit(EXIT_FAILURE);

  /* Socket server initialization */
  int listenfd = initServer();
  
  
  
  
  /* Global variables initialization */
  time( &(gSystemStatus.lastCryoChange) );
  gSystemStatus.cryoStatus=1;

  /* Change the file mode mask */
  umask(0);
          
  /* Open any logs here */
  ofstream logFile(logFileName.c_str());
  logFile << "#Time\tTemp\tPressure\t";
  for(unsigned int p=0;p<gSystemStatus.vTelName.size();++p)
     logFile << gSystemStatus.vTelName[p] << "\t";
  logFile << "cryoStatus\treadingImage\texposingImage";
  logFile << endl;
  
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
    double dif = difftime(currentTime,lastLogTime);
    if(dif < kLogTimeInterval) continue;

    /* Get data from all the sources and write the log */
    getLogData();
    
    logFile << time (NULL) << " " << gSystemStatus.temp << "\t" << gSystemStatus.pres;
    for(unsigned int p=0;p<gSystemStatus.vTelComm.size();++p)
      logFile << "\t" << gSystemStatus.vTel[p];
    logFile << "\t" << gSystemStatus.cryoStatus; 
    logFile << "\t" << gSystemStatus.readingImage;
    logFile << "\t" << gSystemStatus.exposingImage << endl;

    if(gSystemStatus.exposingImage)
      updateExpoStats();
    
    i++;
    time( &lastLogTime ); 
  }
  cout << "after\n";
  logFile.close();
  exit(EXIT_SUCCESS);
}

