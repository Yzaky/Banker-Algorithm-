/* This `define` tells unistd to define usleep and random.  */
#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "client_thread.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <string.h>
#include <arpa/inet.h>

int port_number = -1;
int num_clients = -1;
int num_request_per_client = -1;
int num_resources = -1;
int *provisioned_resources = NULL;

// Variable d'initialisation des threads clients.
unsigned int count = 0; 
// Variable du journal.
// Nombre de requête acceptée (ACK reçus en réponse à REQ)
unsigned int count_accepted = 0; 

// Nombre de requête en attente (WAIT reçus en réponse à REQ)
unsigned int count_on_wait = 0;

// Nombre de requête refusée (REFUSE reçus en réponse à REQ)
unsigned int count_invalid = 0;

// Nombre de client qui se sont terminés correctement (ACC reçu en réponse à END)
unsigned int count_dispatched = 0;

// Nombre total de requêtes envoyées.                          
unsigned int request_sent = 0; 

unsigned int count_refused=0;
//Attente maximum pour tenter de se connecter
int attenteMax = 30;
struct {
int* allocation;
int* maximum;
}T;
pthread_mutex_t _mutex_dispatched;
pthread_mutex_t _mutex_client;
pthread_mutex_t _mutex_denied;
pthread_mutex_t _mutex_request;
pthread_mutex_t _mutex_accepted;
pthread_mutex_t _mutex_notSafe;

// Vous devez modifier cette fonction pour faire l'envoie des requêtes
// Les ressources demandées par la requête doivent être choisies aléatoirement
// (sans dépasser le maximum pour le client). Elles peuvent être positives
// ou négatives.
// Assurez-vous que la dernière requête d'un client libère toute les ressources
// qu'il a jusqu'alors accumulées.
void
send_request (int client_id, int request_id, int socket_fd)
{
  char values[num_resources+2]; 
  char toSend[80];
  pthread_mutex_lock(&_mutex_request);
  request_sent += 1;
  pthread_mutex_unlock(&_mutex_request);
  bzero (&toSend, sizeof(toSend));

  strcat(toSend, "REQ "); // send REQ with the generated resource numbers
  int* ressource = malloc(num_resources * sizeof(int));
  if (request_id == num_request_per_client - 1) { // if it's our last, free ressources held by thread
      int i=0;
    while(i<num_resources)
    {
      bzero(&values,sizeof(values)); 
      ressource[i] = -1 * T.allocation[(client_id * num_resources) + i];
      sprintf(values, "%d", ressource[i++]);
      strcat(strcat(toSend,values), " ");
    }
  
  } else {      //generate randomly 
    int i=0;
    while(i<num_resources)
    {
    bzero(&values,sizeof(values));
    ressource[i] = (random() % (T.maximum[ (client_id*num_resources) +i ] + 1)) - T.allocation[(client_id*num_resources) +i];
    sprintf(values, "%d", ressource[i++]);
     strcat(strcat(toSend, values), " ");
    }   

     }
  strcat(toSend, "\n");
  printf("Client(%d):-Requested--> %s",client_id,toSend);
  send_to_server(socket_fd,toSend);

  char serverAnswer[5];
  bzero(&serverAnswer, sizeof (serverAnswer));
  recv(socket_fd, serverAnswer, sizeof(serverAnswer), 0);
  if(!memcmp(serverAnswer,"ACK",3)==0)
  {

    pthread_mutex_lock(&_mutex_notSafe);
    count_invalid += 1;                       //invalid, would lead to unsafe state.
    pthread_mutex_unlock(&_mutex_notSafe);
  }
    pthread_mutex_lock(&_mutex_accepted);  //accepted
    count_accepted += 1;
      pthread_mutex_unlock(&_mutex_accepted);
    int i=0;
    while(i<num_resources){     
      T.allocation[(client_id * num_resources) + i] += ressource[i];    //update after request accepted
      i++;
    }
  free(ressource);
}


void *
ct_code (void *param)
{

client_thread *ct = (client_thread *) param;
  int socket_fd = socket_creation();
  char toSend[80];
  char values[num_resources+2];
  bzero (&toSend, sizeof (toSend)); // set to 0
  strcat(toSend, "INI "); // Send INI
  int i=0;
  while(i<num_resources){
    bzero (&values, sizeof (values));
    int initialisation = random() % (provisioned_resources[i] + 1);// Generate random numbers for resources to be initialized/t
    T.maximum[(ct->id * num_resources) + i] = initialisation;
    T.allocation[(ct->id * num_resources) + i] = 0;
    sprintf(values, "%d", initialisation);
    strcat(strcat(toSend, values), " ");
    i++;
  }
  strcat(toSend, "\n");
  printf("Client(%d):-Requested--> %s", ct->id,toSend);  
  send_to_server(socket_fd,toSend);
  bzero (&toSend, sizeof (toSend));

  char serverAnswer[5];  //Receiving aknowledge
  bzero (&serverAnswer, sizeof (serverAnswer));
  recv(socket_fd, serverAnswer, sizeof(serverAnswer), 0);

  if (memcmp(serverAnswer, "ACK", 3) == 0) { // Check the request sent by the server
    printf ("Client(%d): is ready..\n", ct->id);
  } 
   else {
    /*perror("ERROR.\n");*/
    pthread_mutex_lock(&_mutex_denied);
    count_refused++;  
    pthread_mutex_unlock(&_mutex_denied);
  }
  bzero(&serverAnswer, sizeof (serverAnswer));

  for (unsigned int request_id = 0; request_id < num_request_per_client;
       request_id++)
    {
      send_request (ct->id, request_id, socket_fd);
      usleep (random () % (100 * 1000));
    }

  printf("Client(%d):-Requested--> CLO\n", ct->id); //Sending Close
  send_to_server(socket_fd,"CLO\n");
  recv(socket_fd, serverAnswer, sizeof(serverAnswer), 0);

  if (memcmp(serverAnswer, "ACK", 3) != 0) {

    perror("ERROR.\n");
  } 
  else 
  {
    printf ("Client(%d) Closed.\n", ct->id);
    pthread_mutex_lock(&_mutex_dispatched);
    count_dispatched += 1;
    pthread_mutex_unlock(&_mutex_dispatched);
  }
  close(socket_fd);

  pthread_exit (NULL);
}

// 
// Vous devez changer le contenu de cette fonction afin de régler le
// problème de synchronisation de la terminaison.
// Le client doit attendre que le serveur termine le traitement de chacune
// de ses requêtes avant de terminer l'exécution. 
//
void
ct_wait_server (int num_clients, client_thread *client_threads)
{



  pthread_mutex_destroy(&_mutex_denied);
  pthread_mutex_destroy(&_mutex_accepted);
  pthread_mutex_destroy(&_mutex_request);
  pthread_mutex_destroy(&_mutex_dispatched);
  pthread_mutex_destroy(&_mutex_client);
  pthread_mutex_destroy(&_mutex_notSafe);
  for(int i = 0; i < num_clients; i++) {
   pthread_join(client_threads[i].pt_tid, NULL);
  }
    free(T.maximum);
  free(T.allocation);
  int socket_fd = socket_creation();

  send_to_server(socket_fd,"END\n");
  char answer[5];
  bzero (&answer, sizeof (answer));
  recv(socket_fd, answer, sizeof(answer), 0);
  if (memcmp(answer, "ACK", 3) == 0) {
    printf ("Server is closed.\n");  
  } else {
    perror("Could not accept the request.\n");
    exit(-1);
  }
}

void send_to_server(int server, char * message_to_send)
{

send(server,message_to_send,strlen(message_to_send),0);

}

void
ct_init (client_thread * ct)
{
  pthread_mutex_init(&_mutex_denied,NULL);
  pthread_mutex_init(&_mutex_accepted,NULL);
  pthread_mutex_init(&_mutex_request,NULL);
  pthread_mutex_init(&_mutex_dispatched,NULL);
  pthread_mutex_init(&_mutex_client,NULL);
  pthread_mutex_init(&_mutex_notSafe,NULL);
  pthread_mutex_lock(&_mutex_client);
  ct->id = count++;
  pthread_mutex_unlock(&_mutex_client);
}

void
ct_create_and_start (client_thread * ct)
{
  pthread_attr_init (&(ct->pt_attr));
  pthread_create (&(ct->pt_tid), &(ct->pt_attr), &ct_code, ct);
}

//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results (FILE * fd, bool verbose)
{
  if (fd == NULL)
    fd = stdout;
  if (verbose)
    {
      fprintf (fd, "\n---- Résultat du client ----\n");
      //fprintf (fd,"Requetes refusée= %d\n",count_refused);
      fprintf (fd, "Requêtes acceptées: %d\n", count_accepted);
      fprintf (fd, "Requêtes : %d\n", count_on_wait);
      fprintf (fd, "Requêtes invalides: %d\n", count_invalid);
      fprintf (fd, "Clients : %d\n", count_dispatched);
      fprintf (fd, "Requêtes envoyées: %d\n", request_sent);
    }
  else
    {
      fprintf (fd, "%d %d %d %d %d\n", count_accepted, count_on_wait,
         count_invalid, count_dispatched, request_sent);
    }
}
//Begin a connection by sending BEG To the server on listening.
void Begin(){
  
  int socket_fd = socket_creation();
  if (socket_fd >0){
  T.allocation = malloc(num_clients * num_resources * sizeof(int)); // allocation
  T.maximum = malloc(num_clients * num_resources * sizeof(int));
  send_Begin_Pro(socket_fd);
  }
  else {

    perror("ERROR binding\n");
    exit(-1);
  }
  close(socket_fd);

}

void send_Begin_Pro(int socket_fd)
{

  char num[num_resources+2], toSend[80];
  bzero (&toSend, sizeof (toSend));
  sprintf(num, "%d", num_resources);
  strcat(strcat(strcat(toSend, "BEG "), num), "\n");
  send_to_server(socket_fd,toSend);

  char answer[5];
  bzero (&answer, sizeof (answer));
  recv(socket_fd, answer, sizeof(answer), 0);

  if (memcmp(answer, "ACK", 3) == 0) {
    printf ("**************Server initialised successfully*************.\n");
  }
  else {
    perror("ERROR.\n");
    exit(-1);
  }
  bzero (&toSend, sizeof (toSend));

  strcat(toSend, "PRO ");
  for (int i = 0; i < num_resources; i++) {
    bzero (&num, sizeof (num));
    sprintf(num, "%d", provisioned_resources[i]);
    strcat(strcat(toSend, num), " ");
  }
  strcat(toSend, "\n");
  send_to_server(socket_fd,toSend);
  bzero (&answer, sizeof (answer));
  recv(socket_fd, answer, sizeof(answer), 0);

  if (memcmp(answer, "ACK", 3) != 0) {
    perror("Réponse du serveur erronée.\n");
    exit(-1);
  }


}
int socket_creation() {

  int socket_fd;
  struct sockaddr_in servAddr;
  socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    perror("ERROR opening socket");
  }
  struct hostent *hst;
  hst = gethostbyname("localhost");
  if (hst == NULL) {
    perror("no such host");
  }
  bzero((char *) &servAddr, sizeof(servAddr));
  servAddr.sin_family = AF_INET;
  bcopy((char *) hst->h_addr_list[0],
        (char*) &servAddr.sin_addr.s_addr,
        hst->h_length);
  servAddr.sin_port = htons(port_number);

  if (connect(socket_fd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
    perror("error connecting");
  }
    return socket_fd;
}