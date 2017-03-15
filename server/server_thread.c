#define _XOPEN_SOURCE 700   /* So as to allow use of `fdopen` and `getline`.  */
#include "server_thread.h"
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

enum { NUL = '\0' };
enum {
  /* Configuration constants.  */
   max_wait_time = 30,
   server_backlog_size = 5
};

unsigned int server_socket_fd;

// Variable du journal.
// Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ).
unsigned int count_accepted = 0;

// Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé).
unsigned int count_wait = 0;

// Nombre de requête erronées (ERR envoyé en réponse à REQ).
unsigned int count_invalid = 0;

// Nombre de clients qui se sont terminés correctement
// (ACK envoyé en réponse à CLO).
unsigned int count_dispatched = 0;

// Nombre total de requête (REQ) traités.
unsigned int request_processed = 0;

// Nombre de clients ayant envoyé le message CLO.
unsigned int clients_ended = 0;

// TODO: Ajouter vos structures de données partagées, ici.
struct {
int numRessources;
int* max;
int* allocation;
int _id;
int _number_connection;
int* available;
int* need;
} banker;
pthread_mutex_t _mutex;

void
st_init ()
{
  int clientRecv = wait_request(); // Wait client connection
  char clientAnswer[80]; 
  recv(clientRecv, clientAnswer, sizeof(clientAnswer), 0);
  int i = 0;
  int* toReturn = malloc(1 * sizeof(int));
  char* c = strtok(&clientAnswer[4], " \n");
  while (c != NULL && strlen(c)!=0) 
  {
      toReturn = realloc(toReturn, (++i+1) * sizeof(int));
      toReturn[i] = atoi(c);
      c = strtok(NULL, " \n");
  }
    toReturn[0] = i;// helps to debug number of arguments read.

  if(toReturn[0]!=1) // Error, PRO request doesn't have arguments //
    exit(-1);
  banker.numRessources = toReturn[1];
  free(toReturn);
  banker.available = malloc(banker.numRessources * sizeof(int)); // ****
  banker.need=malloc(banker.numRessources*sizeof(int));          // Init our 
  banker.max = malloc(banker.numRessources * sizeof(int));        // banker 
  banker.allocation = malloc(banker.numRessources * sizeof(int)); // struct
  pthread_mutex_init(&_mutex, NULL);                         //
  banker._id = 0;                                        //
  banker._number_connection = 0;                                       //*******
  printf("************\nServer initilized with sent %d ressources.\n************\n",banker.numRessources); 
  send_to_client(clientRecv,"ACK"); 
  memset (&clientAnswer, 0, sizeof(clientAnswer)); // clear reception array
  recv(clientRecv, clientAnswer, sizeof(clientAnswer), 0); // receive new request
  int j= 0;
  int* Res = malloc(1 * sizeof(int));
  char* cc = strtok(&clientAnswer[4], " \n");
  while (cc != NULL && strlen(cc)!=0) 
  {
      Res = realloc(Res, (++j+1) * sizeof(int));
      Res[j] = atoi(cc);
      cc = strtok(NULL, " \n");
  }
    Res[0] = j;
  for (int i = 0; i < banker.numRessources; i++)
   {
    banker.available[i] = Res[i+1]; //+1 to ignore <nb_requests>
   }  
  free(Res);
  send_to_client(clientRecv,"ACK\n");
  close(clientRecv);
  // END TODO
}
void

st_process_requests (server_thread * st, int socket_fd)
{
 // TODO: Remplacer le contenu de cette fonction
  FILE *socket_r = fdopen (socket_fd, "r");
  bool init = false;
  int _currentId; // to stock our current id
  while (true) {
    int* available;
    bool* clients;
    int* needbis;
    bool safe;
    bool empty;
    bool waiting = false;
    char cmd[4] = {NUL, NUL, NUL, NUL};
    fread (cmd, 3, 1, socket_r) ;
    char *args = NULL; 
    size_t args_len = 0;
    getline (&args, &args_len, socket_r);
    printf ("Serveur(%d): <--Requested-: %s%s", st->id, cmd, args);
    if (memcmp(cmd, "END", 3) == 0) { //END
      pthread_mutex_lock(&_mutex);
      //stop accepting connections
      accepting_connections = false;
      printf("Serveur fermé avec succès.\n");
     pthread_mutex_unlock(&_mutex);
     send_to_client(socket_fd,"ACK\n");
      break;
    }
    int* params = _getData(args);
    free(args);

    if (memcmp(cmd, "CLO", 3) == 0 && init) {
       bool ressLib = true;
      pthread_mutex_lock(&_mutex);
      clients_ended += 1;
      for (int i = 0; i < banker.numRessources; i++) {
        banker.max[(_currentId*banker.numRessources)+i] = 0;
        if (banker.allocation[(_currentId*banker.numRessources)+i] != 0) {
          ressLib = false;
          banker.available[i] += banker.allocation[(_currentId*banker.numRessources)+i];
          banker.allocation[(_currentId*banker.numRessources)+i] = 0;
        }
      }
      banker._number_connection -= 1;
     pthread_mutex_unlock(&_mutex);
      if (params != NULL || !ressLib) {
        free(params);
        send_to_client(socket_fd,"ERROR!\n");      
        break;
      }
      pthread_mutex_lock(&_mutex);//success
      count_dispatched += 1;
      pthread_mutex_unlock(&_mutex);
      send_to_client(socket_fd,"ACK\n");
      break;
    }
    if (strcmp(cmd, "INI") == 0) {
       pthread_mutex_lock(&_mutex);
      _currentId = banker._id;   //update
      banker._id += 1;
      banker._number_connection += 1;
      banker.max = realloc(banker.max, banker._id * banker.numRessources * sizeof(int));
      banker.allocation = realloc(banker.allocation, banker._id * banker.numRessources * sizeof(int));
      for (int i = 0; i < banker.numRessources; i++) {
        banker.max[(_currentId*banker.numRessources)+i] = params[i+1];
        banker.allocation[(_currentId*banker.numRessources)+i] = 0;
      }
      pthread_mutex_unlock(&_mutex);
      send_to_client(socket_fd,"ACK\n");
      init = true;
      free(params);
      continue;
    }
    //REQ
    if (memcmp(cmd, "REQ", 3) == 0 && init) {
      pthread_mutex_lock(&_mutex);
      request_processed += 1;
      bool valid = true; // check whether the new requested ressources don't exceed our max
      for (int i = 0; i < banker.numRessources; i++) {
        if (params[i+1] > (banker.max[ (_currentId*banker.numRessources)+i] - banker.allocation[ (_currentId*banker.numRessources)+i])
                || params[i+1] < (-1 * banker.allocation[ (_currentId*banker.numRessources)+i])) {
          valid = false;
        }
      }
      pthread_mutex_unlock(&_mutex);
      if (!valid) {
       pthread_mutex_lock(&_mutex);
        count_invalid += 1;
        pthread_mutex_unlock(&_mutex);
        continue;
      }

start:
      pthread_mutex_lock(&_mutex); 
      available = malloc(banker.numRessources * sizeof(int));
      clients = malloc(banker._id * sizeof(bool));
      needbis = malloc(banker._id * banker.numRessources * sizeof(int));
      safe = true;
      empty = false;
      for (int i = 0; i < banker.numRessources; i++) {
        available[i] = banker.available[i] - params[i+1];
      }
      for (int i = 0; i < banker._id; i++) {
        clients[i] = false;
      }
      for (int i = 0; i < banker._id; i++) {
        for (int j = 0; j < banker.numRessources; j++) {
          if (i == _currentId) {
            needbis[(i*banker.numRessources)+j] = banker.max[(i*banker.numRessources)+j] 
                             - banker.allocation[(i*banker.numRessources)+j] - params[j+1];
          } else {
            needbis[(i*banker.numRessources)+j] = banker.max[(i*banker.numRessources)+j]
                                               - banker.allocation[(i*banker.numRessources)+j];
          }
        }
      }
      while (safe && !empty) {
        safe = false;      //BANKER
        empty = true;
        for (int i = 0; i < banker._id; i++) {
          if (!clients[i]) {
            bool done = true;
            for (int j = 0; j < banker.numRessources; j++) {
              if (needbis[(i*banker.numRessources)+j] > available[j]) {
                done = false;
                empty = false;
                break;
              }
            }
            if (done) {
              clients[i] = true;
              for (int j = 0; j < banker.numRessources; j++) {
                if (i == _currentId) {
                  available[j] += banker.allocation[(i*banker.numRessources)+j] + params[j+1];
                } else {
                  available[j] += banker.allocation[(i*banker.numRessources)+j];
                }
              }
              safe = true;
            }
          }
        }
      }
      if (safe) { // if safe, allocate the resources
        for (int i = 0; i < banker.numRessources; i++) {
          banker.available[i] = banker.available[i] - params[i+1];
        }
        for (int i = 0; i < banker.numRessources; i++) {
          banker.allocation[(_currentId*banker.numRessources)+i] += params[i+1];
        }
        if (!waiting) {
          count_accepted += 1;
        } else {
          count_wait += 1;
        }
      }
      pthread_mutex_unlock(&_mutex);
      free(available);
      free(clients);
      free(needbis);
      if (!safe) {  //retry if not safe state
        waiting = true;
        sleep(1);
        goto start;
      }
      //accepted request
       send_to_client(socket_fd,"ACK\n");
      free(params);
      continue;

    }
    free(params);
  }

  fclose (socket_r);
  // TODO end
}


void
st_signal ()
{
  free(banker.available);
  free(banker.max);
  free(banker.allocation);
  free(banker.need);
  pthread_mutex_destroy(&_mutex);
  printf("signal!\n");
}

void *
st_code (void *param)
{
  server_thread *st = (server_thread *) param;

  struct sockaddr_in thread_addr;
  socklen_t socket_len = sizeof (thread_addr);
  int thread_socket_fd = -1;
  int end_time = time (NULL) + max_wait_time;
  while (thread_socket_fd < 0)
    { 
      thread_socket_fd =      //wait receiving
  accept (server_socket_fd, (struct sockaddr *) &thread_addr,
    &socket_len);

      if (time (NULL) >= end_time)
  {
    break;
  }
    }

  while (accepting_connections)//loop as we have requests
    {
      if (time (NULL) >= end_time)
  {
    fprintf (stderr, "Time out on thread %d.\n", st->id);
    pthread_exit (NULL);
  }
      if (thread_socket_fd > 0)
  {
    st_process_requests (st, thread_socket_fd);
    close (thread_socket_fd);
          end_time = time (NULL) + max_wait_time;
  }
      thread_socket_fd =
  accept (server_socket_fd, (struct sockaddr *) &thread_addr,
    &socket_len);
    }
  return NULL;
}


//
// Ouvre un socket pour le serveur.
//
void
st_open_socket (int port_number)
{
  server_socket_fd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (server_socket_fd < 0)
    perror ("ERROR opening socket");

  struct sockaddr_in serv_addr;
  memset (&serv_addr, 0, sizeof (serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons (port_number);

  if (bind
      (server_socket_fd, (struct sockaddr *) &serv_addr,
       sizeof (serv_addr)) < 0)
    perror ("ERROR on binding");

  listen (server_socket_fd, server_backlog_size);
}

//Serving to send messages.
void send_to_client(int client, char * message_to_send)
{

send(client,message_to_send,strlen(message_to_send),0);

}
//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//

void
st_print_results (FILE * fd, bool verbose)
{
  if (fd == NULL) fd = stdout;
  if (verbose)
    {
      fprintf (fd, "\n---- Résultat du serveur ----\n");
      fprintf (fd, "Requêtes acceptées: %d\n", count_accepted);
      fprintf (fd, "Requêtes : %d\n", count_wait);
      fprintf (fd, "Requêtes invalides: %d\n", count_invalid);
      fprintf (fd, "Clients : %d\n", count_dispatched);
      fprintf (fd, "Requêtes traitées: %d\n", request_processed);
    }
  else
    {
      fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_wait,
         count_invalid, count_dispatched, request_processed);
    }
}
int* _getData(char* data) {
  int i = 0;
  int* args = malloc(1 * sizeof(int));
  bool test = false;
  char* c = strtok(data, " \n");
  while (c != NULL) {
    if (strlen(c) != 0) {
      args = realloc(args, (++i+1) * sizeof(int));
      args[i] = atoi(c);
      test = true;
    }
    c = strtok(NULL, " \n");
  }
  if (test) {
    args[0] = i;// numer of args, helped for debugging the PROvision command
    return args;
  } else {
    free(args);
    return NULL;
  }
  
}
//Wait until we receive or client connection
int
wait_request()
{

  struct sockaddr_in addr_client;
  //wait the connection
  socklen_t len = sizeof (addr_client);
  int clientRecv = -1;
  int tempsMax = time(NULL) + max_wait_time;
  while (clientRecv < 0) {

    clientRecv = accept(server_socket_fd, (struct sockaddr*) &addr_client,
           &len);
    if(time(NULL) >= tempsMax) {
      perror("Timeout\n");
      pthread_exit(NULL);
    }
   
  }
 return clientRecv;
}


