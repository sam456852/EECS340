#include "minet_socket.h"
#include <stdlib.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>

#define BUFSIZE 1024
#define FILENAMESIZE 100

int handle_connection(int);
int writenbytes(int,char *,int);
int readnbytes(int,char *,int);

int main(int argc,char *argv[])
{
  int server_port;
  int sock,sock2;
  struct sockaddr_in sa,sa2;
  int rc,i;
  fd_set readlist;
  fd_set connections;
  int maxfd;

  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server2 k|u port\n");
    exit(-1);
  }
  server_port = atoi(argv[2]);
  if (server_port < 1500)
  {
    fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n",server_port);
    exit(-1);
  }

  /* initialize and make socket */
   if (toupper(*(argv[1])) == 'K') { 
  minet_init(MINET_KERNEL);
    } else if (toupper(*(argv[1])) == 'U') { 
  minet_init(MINET_USER);
    } else {
  fprintf(stderr, "First argument must be k or u\n");
  exit(-1);
    }
  sock = minet_socket(SOCK_STREAM);

  /* set server address*/
  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(server_port);
  sa.sin_addr.s_addr = htonl(INADDR_ANY);

  /* bind listening socket */
  if(minet_bind(sock, &sa) < 0){
    fprintf(stderr, "Cannot bind a socket\n");
    minet_close(sock);
    exit(-1);
  }

  /* start listening */
  if(minet_listen(sock, 1) < 0){
    fprintf(stderr, "Cannot listen a socket\n");
    minet_close(sock);
    exit(-1);
  }

  FD_ZERO(&connections);
  FD_ZERO(&readlist);
  FD_SET(sock, &connections);
  maxfd = sock;

  /* connection handling loop */
  while(1)
  {
    /* create read list */
    //readlist = connections;
    /* do a select */
    if(minet_select(maxfd+1, &connections, NULL, NULL, NULL) < 1){
      fprintf(stderr,"Cannot select a socket!\n");
      minet_close(sock);
      exit(-1);
    }
    /* process sockets that are ready */
    //for(i = 0; i<= maxfd; i++){
      //if(FD_ISSET(i, &connections)){
        /* for the accept socket, add accepted connection to connections */
        //if(i == sock){
          if((sock2 = minet_accept(sock, &sa2))){
            FD_SET(sock2, &readlist);
            //if(sock2 > maxfd){
              //maxfd = sock2;
            //}
          }
          else{
            fprintf(stderr, "Cannot accept!\n");
            continue;
          }
        //}
        //else{
          /* for a connection socket, handle the connection */
          rc = handle_connection(sock2);
          FD_CLR(sock2, &readlist);
          //if(i == maxfd){
            //for(int j = maxfd; j > 0; j--){
              //if(FD_ISSET(j, &connections)){
                //maxfd = j;
                //break;
              //}
            //}
          //}
        //}
      //}
    //}
  }
}

int handle_connection(int sock2)
{
  char filename[FILENAMESIZE+1];
  //int rc;
  //int fd;
  struct stat filestat;
  char buf[BUFSIZE+1];
  //char *headers;
  //char *endheaders;
  char *bptr;
  int datalen=0;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"\
                         "</body></html>\n";
  bool ok=true;

  /* first read loop -- get request and headers*/
  if(readnbytes(sock2,buf, BUFSIZE) < 0){
    fprintf(stderr, "Cannot read request!\n");
    minet_close(sock2);
    //要从connections删掉吗？
    exit(-1);
  }

  /* parse request to get file name */
  /* Assumption: this is a GET request and filename contains no spaces*/  
  char *requestMethod = strtok(buf," ");
  if(requestMethod == NULL || strcasecmp(requestMethod, "GET") != 0){
    fprintf(stderr, "Please choose request method!\n");
  }
  char *object = strtok(NULL," \r\n");
  if(object == NULL){
    fprintf(stderr, "Please insert filename\n");
  }
  //delete the '/' if the input filename has it
  if (object[0] == '/'){
    object++;
  }
  char *version = strtok(NULL," \r\n");
  if(version == NULL || strcasecmp(version,"http/1.0") != 0){
    fprintf(stderr, "Please use HTTP/1.0\n");
  }
  //acquire filepath
  memset(filename, 0, FILENAMESIZE + 1); 
  getcwd(filename, FILENAMESIZE);
  filename[strlen(filename)] = '/';//add '/' at the end of file path
  strncpy(filename + strlen(filename), object, strlen(object));
  /* try opening the file */
  if(stat(filename, &filestat) < 0){
    fprintf(stderr, "Cannot find the file!\n");
    ok = false;
  }else{
    datalen = filestat.st_size;
    bptr = (char *)malloc(datalen);
    memset(bptr, 0, datalen);
    FILE *stream = fopen(filename, "r");
    if(fread(bptr, sizeof( char ), datalen, stream) < 0){
      fprintf(stderr, "Cannot read file!\n");
    }
    fclose(stream);
  }
  /* send response */
  if (ok)
  {
    /* send headers */
    snprintf(ok_response, sizeof(ok_response), ok_response_f, datalen);
    if(writenbytes(sock2, ok_response, strlen(ok_response)) < 0){
      fprintf(stderr, "Cannot send headers!\n");
      minet_close(sock2);
      exit(-1);
    }
    /* send file */
    if(writenbytes(sock2, bptr, datalen) < 0){
      fprintf(stderr, "Cannot send file!\n");
      minet_close(sock2);
      exit(-1);
    }else {
      minet_close(sock2);
      return 0;
    }
  }else{	// send error response
    if(writenbytes(sock2, notok_response, strlen(notok_response)) < 0){
      fprintf(stderr, "Cannot send error response!\n");
      minet_close(sock2);
      exit(-1);
    }else{
      minet_close(sock2);
      return 0;
    }
  }

  /* close socket and free space */
  minet_close(sock2);
  free(bptr);
  return -1;
}

int readnbytes(int fd,char *buf,int size)
{
  int rc = 0;
  int totalread = 0;

 while ((rc = minet_read(fd,buf+totalread,size-totalread)) > 0){
    totalread += rc;
    if (totalread >= 2){
        if(buf[totalread-1] == '\n' && buf[totalread-2] == '\r' && buf[totalread-3] == '\n' && buf[totalread-4] == '\r'){
	break;
      }
    }
  }
 if (rc < 0)
  {
    return -1;
  }
  else
    return totalread;
}

int writenbytes(int fd,char *str,int size)
{
  int rc = 0;
  int totalwritten =0;
  while ((rc = minet_write(fd,str+totalwritten,size-totalwritten)) > 0)
    totalwritten += rc;

  if (rc < 0)
    return -1;
  else
    return totalwritten;
}
