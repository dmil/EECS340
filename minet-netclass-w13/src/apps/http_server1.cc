#include "minet_socket.h"
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
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
  int rc;

  /* parse command line args */
  if (argc != 3)
  {
    fprintf(stderr, "usage: http_server1 k|u port\n");
    exit(-1);
  }
  server_port = atoi(argv[2]);
  if (server_port < 1500)
  {
    fprintf(stderr,"INVALID PORT NUMBER: %d; can't be < 1500\n",server_port);
    exit(-1);
  }

  /* initialize and make socket */
  if (toupper(*(argv[1])) == 'K')
  {
    minet_init(MINET_KERNEL);
  } 
  else if (toupper(*(argv[1])) == 'U')
  {
    minet_init(MINET_USER);
  } 
  else 
  {
    fprintf(stderr, "First argument must be k or u\n");
    exit(-1);
  }

  sock = minet_socket(SOCK_STREAM);//sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1)
  {
     minet_perror("Failed to create minet socket!\n");
     exit(-1);
  }
  /* set server address*/
  memset(&sa,0,sizeof sa);
  sa.sin_port=htons(server_port);//sa is the accept socket
  sa.sin_addr.s_addr=htonl(INADDR_ANY);
  sa.sin_family=AF_INET;
  /* bind listening socket */
  rc = minet_bind(sock, &sa);
  if (rc < 0)
  {
     minet_perror("Failed to bind listening socket!\n");
     exit(-1);
  }
  /* start listening */
  rc = minet_listen(sock,5);
  if (rc < 0)
  {
     minet_perror("Failed to start listening!\n");
     exit(-1);
  }
  /* connection handling loop */
  while(1)
  {
    /* handle connections */
    sock2 = minet_accept(sock,&sa2);
    if (sock2 < 0)
    {
      minet_perror("Failed to accept connection!\n");
      continue;//try again
    }
    rc = handle_connection(sock2);
  }
}

int handle_connection(int sock2)
{
  char filename[FILENAMESIZE+1];
  int rc;
  int fd;
  struct stat filestat;
  char buf[BUFSIZE+1];
  char *headers;
  char *endheaders;
  char *bptr;
  int datalen=0;
  char *ok_response_f = "HTTP/1.0 200 OK\r\n"\
                      "Content-type: text/plain\r\n"\
                      "Content-length: %d \r\n\r\n";
  char ok_response[100];
  char *notok_response = "HTTP/1.0 404 FILE NOT FOUND\r\n"\
                         "Content-type: text/html\r\n\r\n"\
                         "<html><body bgColor=black text=white>\n"\
                         "<h2>404 FILE NOT FOUND</h2>\n"
                         "</body></html>\n";
  bool ok=true;

  /* first read loop -- get request and headers*/
  endheaders = "\r\n\r\n";
  memset(buf, 0, sizeof(buf));
  while ((rc = minet_read(sock2,buf,BUFSIZE)) > 0)
  {
    buf[rc] = '\0';
    if (strtok(buf, endheaders) != NULL) //valid header
    {
      headers = buf;
      break;
    }
  }
  if (rc < 0)
  {
      minet_perror("Failed to read client request!\n");
      minet_close(sock2);
      return -1;
  }
  /* parse request to get file name */
  /* Assumption: this is a GET request and filename contains no spaces*/
  bptr = strtok(buf, " ");
  if ( (strcmp(bptr, "GET") == 0) || 
       (strcmp(bptr, "HEAD") == 0) ||
       (strcmp(bptr, "DELETE") == 0) ||
       (strcmp(bptr, "PUT") == 0) ||
       (strcmp(bptr, "POST") == 0) ||
       (strcmp(bptr, "PATCH") == 0) ||
       (strcmp(bptr, "OPTIONS") == 0))
  {
      bptr = strtok(NULL, " "); // get the filename after request
      if (bptr[0] == '/')
         bptr += 1; //move pointer after the '/'
      else if (strlen(bptr) > FILENAMESIZE)
      {
         fprintf(stderr, "Not a valid filename!\n");
         minet_close(sock2);
         return -1;
      }
  }
  else
  {
      minet_perror("Not a valid request!\n");
      minet_close(sock2);
      return -1;
  } 
  strcpy(filename, bptr);
  /* try opening the file */
  fd = open(filename,O_RDWR);
  if (fd < 0)
  {
    ok = false;
    fprintf(stderr, "Failed to open file!\n");
  }
  else
    ok = true;
  /* send response */
  if (ok)
  {
    /* send headers */
    rc = stat(filename,&filestat);
    if (rc < 0)
    {
      fprintf(stderr, "Failed to get file stat!\n");
      minet_close(sock2);
      return -1;
    }
    sprintf(ok_response,ok_response_f,filestat.st_size);
    rc = writenbytes(sock2,ok_response,strlen(ok_response));
    if (rc < 0)
    {
      minet_perror("Failed to send response!\n");
      minet_close(sock2);
      return -1;
    }
    /* send file */
    datalen=0;
    while ((rc = minet_read(fd,buf,BUFSIZE)) != 0)
    {
      if( datalen == filestat.st_size ) break; //send required file  
      else if (rc < 0)
      {
	minet_perror("Failed to buffering requested file!\n");
	ok = false;
        break;
      }
      else
      {
	rc = writenbytes(sock2, buf, rc);
        datalen += rc; //accumulate how much required data already written
	if (rc < 0)
        {
	  minet_perror("Failed to send file!\n");
	  minet_close(sock2);
	  return -1;
	}
      }
    }
  }
  else // send error response
  {
    rc = writenbytes(sock2,notok_response,strlen(notok_response));
    if (rc < 0)
    {
      minet_perror("Failed to send error response!\n");
      minet_close(sock2);
      return -1;
    }
  }

  /* close socket and free space */
  free(headers);
  free(endheaders);
  free(bptr); 
  minet_close(sock2);
  if (ok)
    return 0;
  else
    return -1;
}

int readnbytes(int fd,char *buf,int size)
{
  int rc = 0;
  int totalread = 0;
  while ((rc = minet_read(fd,buf+totalread,size-totalread)) > 0)
    totalread += rc;

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

