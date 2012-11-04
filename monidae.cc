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
#include <sys/file.h>

using namespace std;

//const char logFileName[]="/home/javier/Dropbox/Damic/tools/monitorDaemon/log.txt";
//const char lockFile[]="/home/javier/Dropbox/Damic/tools/monitorDaemon/lock";

const char logFileName[]="/damicData/javier/monitorDaemon/log.txt";
const char lockFile[]="/damicData/javier/monitorDaemon/lock";

const char inetAddr[] = "127.0.0.1";
//const char inetAddr[] = "131.225.90.254"; //whirl
//const char inetAddr[] = "131.225.90.5"; //cyclone
const int portTemp = 2055;
const int portPres = 2050;
time_t lastCryoChange(0);
const long kMinTimeCryoChange = 600; //10 minutes
const long kLogTimeInterval = 30; //in seconds

bool gCryoStatus;


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
  double dif = difftime(currentTime,lastCryoChange);
  
  if(dif>kMinTimeCryoChange){
    time(&lastCryoChange);
    gCryoStatus = cryoON;
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
  //else cout << recvBuff << "||\n";
  
  close(client_sock);
  usleep(10000);
  return 0;
}

int main(void) {
  
  int pid_file = open(lockFile, O_CREAT | O_RDWR, 0666);
  int rc = flock(pid_file, LOCK_EX | LOCK_NB);
  if(rc) {
  if(EWOULDBLOCK == errno)
    cout << "\nThere is another instance of the DAMIC daemon running!\n";
    cout << "Kill it before starting a new one.\n";
    cout << "No new instances will be launched.\n\n";
    return 1; // another instance is running
  }
  
  /* Our process ID and Session ID */
  pid_t pid, sid;

  /* Fork off the parent process */
  pid = fork();
  if (pid < 0) {
          exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then
      we can exit the parent process. */
  if (pid > 0) {
          exit(EXIT_SUCCESS);
  }
          
  /* Create a new SID for the child process */
  sid = setsid();
  if (sid < 0) {
          /* Log the failure */
          exit(EXIT_FAILURE);
  }

  /* Change the current working directory */
  if ((chdir("/")) < 0) {
          /* Log the failure */
          exit(EXIT_FAILURE);
  }

  /* Close out the standard file descriptors */
//   close(STDIN_FILENO);
//   close(STDOUT_FILENO);
//   close(STDERR_FILENO);

  /* Daemon-specific initialization goes here */
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
  
  time(&lastCryoChange);
  gCryoStatus=1;

  /* Change the file mode mask */
  umask(0);
          
  /* Open any logs here */
  ofstream logFile(logFileName);
  logFile << "#Time\tTemp\tPressure\n";
  
  /* The Big Loop */
  int i=0;
 
  time_t lastLogTime = 0;
 
  while (true) {

    if(listenForCommands(listenfd) == -1){
      break;
    }
    usleep(500000); /* wait 1 second */

    time_t currentTime;
    time( &currentTime);
    double dif = difftime(currentTime,lastLogTime);
    if(dif < kLogTimeInterval) continue;

    /* Get data from all the sources and write the log */
    const string msjTemp("rtd");
    string responseTemp("");
    int comCode = talkToSocket(inetAddr, portTemp, msjTemp, responseTemp);
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responseTemp = oss.str();
    }
    
    const string msjPres("prs");
    string responsePres("");
    comCode = talkToSocket(inetAddr, portPres, msjPres, responsePres);
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responsePres = oss.str();
    }
    
    const int portPan = 5355;
    const string msjPan("att get SL3_B_V2_LowDac");
    string responsePan("");
    comCode = talkToSocket(inetAddr, portPan, msjPan, responsePan);
    if ( comCode < 0){
      ostringstream oss;
      oss << comCode;
      responsePan = oss.str();
    }
    //cout << "Pan: "<< responsePan << endl;
    
    logFile << time (NULL) << " " << responseTemp << "\t" << responsePres << "\t" << responsePan 
            << "\t" << gCryoStatus << endl;
    i++;
    time( &lastLogTime ); 
  }
  cout << "after\n";
  logFile.close();
  exit(EXIT_SUCCESS);
}

