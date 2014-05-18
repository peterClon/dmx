//#define _GNU_SOURCE
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define PORT 5568
#define BUFSIZE 1024
#define MAXCHAN 512

volatile char channels[MAXCHAN];
int cnt = 0;
int chans = 20;

int child();
int e131();
int e131m();
int setbaud(int fd, speed_t baud);
int sendchans();
void help();
int setserial(int fd);

char defaultdev[] = "/dev/ttyUSB0";
char *devname = defaultdev;
//speed_t dmxspeed=B250000;
int mabval = 12, mbsval = 0, mbbval = 0, universe=0;
bool realBreak = false, network = false, multicast=false, Tpause=false;
char realzero=0, startzero = 0, initval = 0;

#define CHILDSTACK 65536
#define DEVNAME "/dev/ttyUSB0"
#define PIPENAME "/var/dmx/dmxin"
#define PIPEOUT "/var/dmx/dmxout"

#define soptions "a:s:p:i:d:c:t:u:bhem"

static struct option const long_options[] =
{
   { "break", no_argument, NULL, 'b' },      //Real BREAK or simulated.
   { "mab", required_argument, NULL, 'a' },  //MARK after break. (Delay after break)
   { "mbs", required_argument, NULL, 's' },  //delay between bytes.
   { "mbb", required_argument, NULL, 'p' },  //delay after packet.
   { "init", required_argument, NULL, 'i' }, //starting values for channels.
   { "channels", required_argument, NULL, 'c' }, // Channels device.
   { "starttype", required_argument, NULL, 't' }, // Start Byte for packet(zero).
   { "device", required_argument, NULL, 'd' }, //Serial device.
   { "e131", no_argument, NULL, 'e' }, //Unicast Network.
   { "e131m", no_argument, NULL, 'm' }, //Multicast Network.
   { "universe", required_argument, NULL, 'u' }, //universe that we're tied to.
   { "help", no_argument, NULL, 'h' }, // Show Help.
   { NULL, no_argument, NULL, 0 }
};

main(int argc, char *argv[]) {
   int i, c, ichan, ival, mabval = 12, mbsval = 1, mbbval;
   FILE *infile;

   while ((c = getopt_long(argc, argv, soptions, long_options, NULL)) != -1)
   {
      switch (c)
      {
      case 'b':
         realBreak = true;
         break;
      case 'e':
         network = true;
         printf("Networking Enabled\n");
         break;
      case 'm':
         multicast = true;
         printf("Multicast Networking Enabled\n");
         break;
      case 'a':
         mabval = atoi(optarg);
         break;
      case 's':
         mbsval = atoi(optarg);
         break;
      case 'p':
         mbbval = atoi(optarg);
         break;
      case 'u':
         universe = atoi(optarg);
         break;
      case 'i':
         initval = (char)atoi(optarg);
         break;
      case 'c':
         chans = atoi(optarg);
         break;
      case 't':
         startzero = (char)atoi(optarg);
         break;
      case 'd':
         devname = optarg;
         break;
      case 'h':
         help();
         break;
      default:
         break;
      }
   }

   for (i = 0; i < chans; i++) channels[i] = initval;

   void *child_stack = malloc(CHILDSTACK);
   if (child_stack == 0) printf("Malloc Failed\n");
   child_stack += CHILDSTACK;
   if (clone(child, child_stack, CLONE_THREAD | CLONE_SIGHAND | CLONE_VM, argv[0]) == -1)
   {error(1, errno, "Clone Failed");}

   void *send_stack = malloc(CHILDSTACK);
   if (send_stack == 0) printf("Malloc Failed\n");
   send_stack += CHILDSTACK;
   if (clone(sendchans, send_stack, CLONE_THREAD | CLONE_SIGHAND | CLONE_VM, argv[0]) == -1)
   {error(1, errno, "Clone Failed");}

   if (network)
   {
      void *network_stack = malloc(CHILDSTACK);
      if (network_stack == 0) printf("Network Malloc Failed\n");
      network_stack += CHILDSTACK;
      if (clone(e131, network_stack, CLONE_THREAD | CLONE_SIGHAND | CLONE_VM, argv[0]) == -1)
      {error(1, errno, "Network Clone Failed");}
   }

   if (multicast)
   {
      void *multicast_stack = malloc(CHILDSTACK);
      if (multicast_stack == 0) printf("Network Malloc Failed\n");
      multicast_stack += CHILDSTACK;
      if (clone(e131m, multicast_stack, CLONE_THREAD | CLONE_SIGHAND | CLONE_VM, argv[0]) == -1)
      {error(1, errno, "Multicast Clone Failed");}
   }

   infile = fopen(PIPENAME, "r");
   if (infile != NULL)
   {
      for (;;)
      {
         i = fscanf(infile, "%d:%d", &ichan, &ival);
         if (i < 0)
         {
            fclose(infile);
            infile = fopen(PIPENAME, "r");
         }
         if (i == 2)
         {
            Tpause=false;
            if (ichan == 0 && ival == 0) exit(1);
            if (ichan == 0 && ival == 2) Tpause=true;
            //if (ichan == 0 && ival == 1) sendchans();
            if (ichan > 0 && ichan <= chans && ival >= 0 && ival <= 255)
            {
               channels[ichan - 1] = (char)ival;
            }
         }
      }
   } else
   {
      printf("unable to open: %s", PIPENAME);
      exit(2);
   }
}

int sendchans() {
   int i;
   FILE *outfile;

   for(;;)
   {
    outfile = fopen(PIPEOUT, "w");
    fprintf(outfile, "");  //this will block until someone reads the pipe.

    for (i = 0; i < chans; i++)
    {
      fprintf(outfile, "%d:", channels[i]);
    }
    fprintf(outfile, "\n");  //end line
    fclose(outfile);
    sleep(1); //hack - need to know how to ensure that the link is closed.
   }
}


int child() {
   int fd, i;
   char outb;

   fd = open(devname, O_WRONLY |  O_NONBLOCK);
   if (fd < 0)
   {
      perror(devname);
      return 1;
   }

   if (setserial(fd))
   {
      perror(devname);
      return 1;
   }

   for (;;)
   {
      while(Tpause)
      {
       sleep(1); //hack - should wait for semaphore.
      }
      tcdrain(fd);
      if (mbbval)
      {
         usleep(mbbval);
      }
      if (realBreak)
      {
         tcsendbreak(fd, 1); //send real break.
      } else
      {
         setbaud(fd, B115200); //send break by altering baud rate.
         write(fd, &realzero, 1);
         tcdrain(fd);
         setbaud(fd, B500000);
      }
      tcdrain(fd);
      usleep(mabval);
      write(fd, &startzero, 1);
      for (i = 0; i < chans; i++)
      {
         outb = channels[i];
         if (mbsval)
         {
            tcdrain(fd);
            usleep(mbsval);
         }
         write(fd, &outb, 1);
      }
   }
}

int setserial(int fd) {
   int r;
   struct termios termios;

   if (!isatty(fd))
   {
      printf("Not a valid serial device\n");
      return -ENOTTY;
   }

   r = tcgetattr(fd, &termios);
   if (r < 0)
   {
      printf("setserial: get attributes failed\n");
      return -1;
   }

   cfmakeraw(&termios);          //raw mode
   termios.c_cflag |= CSTOPB;   //Two stop bits

   r = tcsetattr(fd, TCSANOW, &termios);
   if (r < 0)
   {
      printf("setserial: tcsetattr() failed\n");
      return -1;
   }
   return 0;
}

int setbaud(int fd, speed_t baud) {
   struct termios termios;

   tcgetattr(fd, &termios);

   cfsetospeed(&termios, baud);
   cfsetispeed(&termios, baud);

   tcsetattr(fd, TCSANOW, &termios);

   return 0;
}

void help() {
   printf("DMX User Space Driver version 0.03\n");
   printf("--break, user standard BREAK\n");
   printf("--mab=n set MARK after BREAK value (microseconds)\n");
   printf("--mbs=n, set delay between channels (microseconds)\n");
   printf("--mbb=n, set delay after packet (microseconds)\n");
   printf("--init=n, starting value for channels, deafult is zero\n");
   printf("--channels=n, set the count of channels to transmit, default is %d\n", chans);
   printf("--starttype=n, value for first byte sent\n");
   printf("--device=<device>, serial device to use, default is %s\n", defaultdev);
   printf("--e131, enable unicast e1.31 network support\n");
   printf("--e131m, enable multicast e1.31 network support\n");
   printf("--universe, multicast universe, default is one\n");
   printf("--help, show help\n");
   exit(0);
}

e131() {
   
   int i,sd, rc, n, cliLen;
   struct sockaddr_in cliAddr, servAddr;
   char msg[BUFSIZE];
   ssize_t received;

   sd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
   if (sd < 0)
   {
      printf("Cannot open socket\n");
      exit(1);
   }

   // bind 
   servAddr.sin_family = AF_INET;
   servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
   servAddr.sin_port = htons(PORT);
   if (bind(sd, (struct sockaddr *)&servAddr, sizeof(servAddr)) < 0)
   {
      printf("Cannot Bind\n");
      exit(1);
   }
   cliLen = sizeof(cliAddr);

   for (;;)
   {

      if ((received = recvfrom(sd, msg, BUFSIZE, 0, (struct sockaddr *)&cliAddr, &cliLen)) < 0)
      {
         printf("Error Receiving\n");
      }

      if (received > 125)
      {
         startzero=msg[125];
         received-=126;
         received=received<chans?received:chans;
         for (i=0;i<received;i++)
         {
            channels[i] = (char)msg[i+126];
         }
      }
//      printf("from %s:UDP%u : :size:%d\n", inet_ntoa(cliAddr.sin_addr), ntohs(cliAddr.sin_port), received);

   }

   return 0;

}

#define MAX_LEN  1024   /* maximum receive string size */

e131m()
{
  int sock;                     /* socket descriptor */
  int flag_on = 1;              /* socket option flag */
  struct sockaddr_in mc_addr;   /* socket address structure */
  char recv_str[MAX_LEN+1];     /* buffer to receive string */
  int recv_len;                 /* length of string received */
  struct ip_mreq mc_req;        /* multicast request structure */
  char ipbuffer[16];    //255.255.255.255
  char* mc_addr_str=ipbuffer;    /* multicast IP address */
  unsigned short mc_port=PORT;       /* multicast port */
  struct sockaddr_in from_addr; /* packet source */
  unsigned int from_len;        /* source addr length */
  int i;

  universe=universe?universe:1;
  snprintf(mc_addr_str, 16, "239.255.0.%d", universe);
  printf("Multicast on %s\n",mc_addr_str );
  

  /* create socket to join multicast group on */
  if ((sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("socket() failed");
    exit(1);
  }

  /* set reuse port to on to allow multiple binds per host */
  if ((setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &flag_on,
       sizeof(flag_on))) < 0) {
    perror("setsockopt() failed");
    exit(1);
  }

  /* construct a multicast address structure */
  memset(&mc_addr, 0, sizeof(mc_addr));
  mc_addr.sin_family      = AF_INET;
  mc_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  mc_addr.sin_port        = htons(mc_port);

  /* bind to multicast address to socket */
  if ((bind(sock, (struct sockaddr *) &mc_addr,
       sizeof(mc_addr))) < 0) {
    perror("bind() failed");
    exit(1);
  }

  /* construct an IGMP join request structure */
  mc_req.imr_multiaddr.s_addr = inet_addr(mc_addr_str);
  mc_req.imr_interface.s_addr = htonl(INADDR_ANY);

  /* send an ADD MEMBERSHIP message via setsockopt */
  if ((setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
       (void*) &mc_req, sizeof(mc_req))) < 0) {
    perror("setsockopt() failed");
    exit(1);
  }

  for (;;) {          /* loop forever */

    from_len = sizeof(from_addr);

    /* block waiting to receive a packet */
    if ((recv_len = recvfrom(sock, recv_str, MAX_LEN, 0,
         (struct sockaddr*)&from_addr, &from_len)) < 0) {
      perror("recvfrom() failed");
      exit(1);
    }

    if (recv_len > 125)
    {
       startzero=recv_str[125];
       recv_len-=126;
       recv_len=recv_len<chans?recv_len:chans;
       for (i=0;i<recv_len;i++)
       {
          channels[i] = (char)recv_str[i+126];
       }
    }

    }

  /* send a DROP MEMBERSHIP message via setsockopt */
  if ((setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
       (void*) &mc_req, sizeof(mc_req))) < 0) {
    perror("setsockopt() failed");
    exit(1);
  }

  close(sock);
}




