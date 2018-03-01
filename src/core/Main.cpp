#include <cstdlib>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include "Globals.h"
#include "Pouch.h"

#include "NetUtils.h"
#include "Main.h"

static void
ignore_sigpipe(void)
{
        // ignore SIGPIPE (or else it will bring our program down if the client
        // closes its socket).
        // NB: if running under gdb, you might need to issue this gdb command:
        //          handle SIGPIPE nostop noprint pass
        //     because, by default, gdb will stop our program execution (which we
        //     might not want).
        struct sigaction sa;
 
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
 
        if (sigemptyset(&sa.sa_mask) < 0 || sigaction(SIGPIPE, &sa, 0) < 0) {
                perror("Could not ignore the SIGPIPE signal");
                exit(EXIT_FAILURE);
        }
}

static void cont_accept_cb(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen, void *ctx)
{
  bufferevent *bev = bufferevent_socket_new(evBase,fd,BEV_OPT_CLOSE_ON_FREE);
  char rejectmsg[100];
  memset(rejectmsg,'\0',100);
  sprintf(rejectmsg,"Go away\n");
  bufferevent_write(bev,&rejectmsg,sizeof(rejectmsg));
}

int main(int argc, char *argv[])
{
  ignore_sigpipe();
  readConfigurationFile();

  printf("Trying to connect to %s\n",DB_SERVER);
  pouch_request *pr = pr_init();
  pr = db_get(pr, DB_SERVER, DB_BASE_NAME);
  pr_do(pr);
  if(pr->httpresponse != 200){
    lprintf("Unable to connect to database. error code %d\n",(int)pr->httpresponse);
    lprintf("CURL error code: %d\n", pr->curlcode);
    exit(0);
  }
  else{
    lprintf("Connected to database: http response code %d\n",(int)pr->httpresponse);
  }
  pr_free(pr);
  
  lprintf("current location is %d\n",CURRENT_LOCATION);

  int err = setupListeners();
  if (err){
    lprintf("There was a problem opening the ports. Is the xl3-server or another instance of penn_daq running?\n");
    exit(0);
  }

  lprintf("\nNAME\t\tPORT#\n");
  lprintf("XL3s\t\t%d-%d\n", XL3_PORT, XL3_PORT+MAX_XL3_CON-1);
  lprintf("SBC/MTC\t\t%d\n", SBC_PORT);
  lprintf("CONTROLLER\t%d\n", CONT_PORT);
  lprintf("VIEWERs\t\t%d\n\n", VIEW_PORT);
  lprintf("waiting for connections...\n");



  pthread_mutex_init(&startTestLock,NULL);

  event_base_dispatch(evBase);

  return 0;
}
