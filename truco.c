#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>

#define MESSAGE_TYPE_BAT          0x1
#define MESSAGE_TYPE_FACED_CARD   0x2
#define MESSAGE_TYPE_SEND_CARD    0x3
#define MESSAGE_TYPE_PLAY         0x4

struct message {
  char source;
  char dest;
  unsigned char read_flags;
  unsigned char sequence;
  unsigned char card;
  unsigned char type;
};

struct connection_data {
  int sock;
  struct sockaddr_in recaddr, sndaddr;
};

static unsigned char current_seq = 0;

static char cards_tag[] = {
  '4',
  '5',
  '6',
  '7',
  'Q',
  'J',
  'K',
  'A',
  '2',
  '3'
};

static char cards_suit[][7] = {
  "Ouros",
  "Espadas",
  "Copas",
  "Paus"
};

double timestamp() {
  struct timeval tp;

  gettimeofday(&tp, NULL);
  return ((double)(tp.tv_sec + tp.tv_usec/1000000.0));
}

int confighost(struct connection_data *cd) {
  struct hostent *h;

  if((cd->sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket");
    return -1;
  }

  if((h = gethostbyname(MACHINE_HOST)) == NULL) {
    perror("gethostbyname");
    return -2;
  }

  cd->recaddr.sin_family = AF_INET;
  cd->recaddr.sin_port = htons(MACHINE_SOURCE_PORT);
  memset(cd->recaddr.sin_zero, 0, sizeof(cd->recaddr.sin_zero));
  memcpy(&cd->recaddr.sin_addr, h->h_addr_list[0], h->h_length);

  if(bind(cd->sock, (struct sockaddr *) &cd->recaddr, sizeof(struct sockaddr_in)) < 0) {
    perror("bind");
    return -3;
  }

  if((h = gethostbyname(MACHINE_DEST_HOST)) == NULL) {
    perror("gethostbyname");
    return -4;
  }

  cd->sndaddr.sin_family = AF_INET;
  cd->sndaddr.sin_port = htons(MACHINE_DEST_PORT);
  memset(cd->sndaddr.sin_zero, 0, sizeof(cd->sndaddr.sin_zero));
  memcpy(&cd->sndaddr.sin_addr, h->h_addr_list[0], h->h_length);
  return 0;
}

int is_card_avaiable(unsigned char card) {
  return !(card & 0x80);
}

unsigned char get_card_tag(unsigned char card) {
  return (card >> 2) & 0x10;
}

unsigned char get_card_suit(unsigned char card) {
  return card & 0x3;
}

void show_cards(unsigned char *my_cards) {
  unsigned int i;
  unsigned char tag, suit;

  for(i = 0; i < 3; ++i) {
    if(is_card_avaiable(my_cards[i])) {
      tag = get_card_tag(my_cards[i]);
      suit = get_card_suit(my_cards[i]);
      fprintf(stdout, "(%d) %c de %s\t", i + 1, cards_tag[tag], cards_suit[suit]);
    }
  }

  fprintf(stdout, "\n");
}

void flush_cards(unsigned char *cards, unsigned int ncards) {
  char picked_cards[40];
  unsigned char random_card;
  unsigned int i;

  for(i = 0; i < sizeof(picked_cards); ++i) {
    picked_cards[i] = 0;
  }

  for(i = 0; i < ncards; ++i) {
    do {
      random_card = rand() % sizeof(picked_cards);
    } while(picked_cards[random_card] == 1);

    cards[i] = random_card;
    picked_cards[random_card] = 1;
  }
}

void send_message(struct connection_data *condata, char dest, unsigned char card, unsigned char type) {
  struct message msg, answer;
  socklen_t recvlen;

  msg.source = MACHINE_TAG;
  msg.dest = dest;
  msg.card = card;
  msg.type = type;
  msg.sequence = current_seq;
  msg.read_flags = READ_FLAG;

  do {
    sendto(condata->sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata->sndaddr, sizeof(struct sockaddr_in));
    recvfrom(condata->sock, &answer, sizeof(struct message), 0, (struct sockaddr *) &condata->recaddr, &recvlen);
  } while(answer.sequence != current_seq || answer.read_flags != 0xF);

  current_seq = (current_seq + 1) % 0xFF;
}

void distribute_cards(struct connection_data *condata, unsigned char *my_cards) {
  unsigned char game_cards[13];
  unsigned int i;

  flush_cards(game_cards, sizeof(game_cards));

  for(i = 0; i < 3; ++i) {
    my_cards[i] = game_cards[i];
  }

  for(i = 3; i < 6; ++i) {
    send_message(condata, 'B', game_cards[i], MESSAGE_TYPE_SEND_CARD);
  }

  for(i = 6; i < 9; ++i) {
    send_message(condata, 'C', game_cards[i], MESSAGE_TYPE_SEND_CARD);
  }

  for(i = 9; i < 12; ++i) {
    send_message(condata, 'D', game_cards[i], MESSAGE_TYPE_SEND_CARD);
  }

  send_message(condata, 0, game_cards[12], MESSAGE_TYPE_FACED_CARD);
}

int main(int argc, const char *argv[]) {
  struct connection_data condata;
  struct message msg;
  unsigned char my_cards[3];
  unsigned char faced_card, tag, suit, count = 0;
  unsigned int card_index;
  socklen_t recvlen;

  if(confighost(&condata) < 0) {
    return -1;
  }

  srand(time(NULL));

  #ifdef START_BAT

  fprintf(stdout, "Embaralhando e distribuindo cartas...\n");
  distribute_cards(&condata, my_cards);
  fprintf(stdout, "Feito!\n");

  fprintf(stdout, "Sua vez, escolha sua carta:\n");
  show_cards(my_cards);
  fprintf(stdout, "> ");
  fscanf(stdin, "%d", &card_index);

  send_message(&condata, 0, my_cards[card_index - 1], MESSAGE_TYPE_PLAY);
  fprintf(stdout, "Carta jogada!\n");

  my_cards[card_index - 1] |= 0x80;
  send_message(&condata, 0, 0, MESSAGE_TYPE_BAT);

  #endif

  while(1) {
    recvfrom(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.recaddr, &recvlen);

    if(msg.sequence == current_seq && !(msg.read_flags & READ_FLAG)) {
      msg.read_flags |= READ_FLAG;

      if(msg.dest == 0 || msg.dest == MACHINE_TAG) {
        switch(msg.type) {
          case MESSAGE_TYPE_BAT:
            fprintf(stdout, "Sua vez, escolha sua carta:\n");
            show_cards(my_cards);
            fprintf(stdout, "> ");
            fscanf(stdin, "%u", &card_index);

            send_message(&condata, 0, my_cards[card_index - 1], MESSAGE_TYPE_PLAY);
            fprintf(stdout, "Carta jogada!\n");

            my_cards[card_index - 1] |= 0x80;
            send_message(&condata, 0, 0, MESSAGE_TYPE_BAT);
            break;

          case MESSAGE_TYPE_FACED_CARD:
            faced_card = msg.card;
            tag = get_card_tag(faced_card);
            suit = get_card_suit(faced_card);
            fprintf(stdout, "Carta virada: %c de %s\n", cards_tag[tag], cards_suit[suit]);
            break;

          case MESSAGE_TYPE_SEND_CARD:
            my_cards[count] = msg.card;
            count = (count + 1) % 3;

            if(count == 0) {
              fprintf(stdout, "Suas cartas:\n");
              show_cards(my_cards);
            }

            break;

          case MESSAGE_TYPE_PLAY:
            tag = get_card_tag(msg.card);
            suit = get_card_suit(msg.card);
            fprintf(stdout, "Jogador %c jogou %c de %s\n", msg.source, cards_tag[tag], cards_suit[suit]);
            break;
        }
      }

      current_seq = (current_seq + 1) % 0xFF;
    }

    sendto(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.sndaddr, sizeof(struct sockaddr_in));
  }

  return 0;
}
