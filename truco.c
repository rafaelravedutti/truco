#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <time.h>

#define MESSAGE_TYPE_BAT            0x1
#define MESSAGE_TYPE_FACED_CARD     0x2
#define MESSAGE_TYPE_SEND_CARD      0x3
#define MESSAGE_TYPE_PLAY           0x4
#define MESSAGE_TYPE_RESET_WINNER   0x5

#define NO_CARD                     ((unsigned char) -1)

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

static char cards_suit[][8] = {
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
  return (card >> 2) & 0x1F;
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

void distribute_cards(struct connection_data *condata, unsigned char *my_cards, unsigned char *faced_card) {
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

  *faced_card = game_cards[12];
  send_message(condata, 0, game_cards[12], MESSAGE_TYPE_FACED_CARD);
}

void process_play(char card_owner, unsigned char card, unsigned char faced_card, char *winner, unsigned char *highest_card) {
  unsigned char card_tag, highest_tag, faced_tag, best_tag;

  if(*highest_card == NO_CARD) {
    *highest_card = card;
    *winner = card_owner;
    return;
  }

  card_tag = get_card_tag(card);
  highest_tag = get_card_tag(*highest_card);
  faced_tag = get_card_tag(faced_card);
  best_tag = (faced_tag + 1) % 10;

  if(highest_tag != best_tag) {
    if(card_tag == best_tag || card_tag > highest_tag) {
      *highest_card = card;
      *winner = card_owner;
    }
  } else {
    if(card_tag == best_tag && get_card_suit(card) > get_card_suit(*highest_card)) {
      *highest_card = card;
      *winner = card_owner;
    }
  }
}

void play_card(struct connection_data *condata, unsigned char *my_cards, unsigned char faced_card, char *winner, unsigned char *highest_card) {
  unsigned char tag, suit;
  unsigned int card_index;

  fprintf(stdout, "Sua vez, escolha sua carta:\n");
  show_cards(my_cards);

  do {
    fprintf(stdout, "> ");
    fscanf(stdin, "%u", &card_index);
  } while(card_index < 1 || card_index > 3 || !is_card_avaiable(my_cards[card_index - 1]));

  tag = get_card_tag(my_cards[card_index - 1]);
  suit = get_card_suit(my_cards[card_index - 1]);
  fprintf(stdout, "Jogador %c jogou %c de %s\n", MACHINE_TAG, cards_tag[tag], cards_suit[suit]);
  send_message(condata, 0, my_cards[card_index - 1], MESSAGE_TYPE_PLAY);

  process_play(MACHINE_TAG, my_cards[card_index - 1], faced_card, winner, highest_card);
  my_cards[card_index - 1] |= 0x80;
}

int main(int argc, const char *argv[]) {
  struct connection_data condata;
  struct message msg;
  char winner;
  unsigned char my_cards[3];
  unsigned char faced_card, highest_card, tag, suit, count = 0;
  socklen_t recvlen;

  if(confighost(&condata) < 0) {
    return -1;
  }

  highest_card = NO_CARD;
  srand(time(NULL));

  #ifdef START_BAT

  fprintf(stdout, "Embaralhando e distribuindo cartas...\n");
  distribute_cards(&condata, my_cards, &faced_card);
  fprintf(stdout, "Feito!\n");

  tag = get_card_tag(faced_card);
  suit = get_card_suit(faced_card);
  fprintf(stdout, "Carta virada: %c de %s\n", cards_tag[tag], cards_suit[suit]);

  play_card(&condata, my_cards, faced_card, &winner, &highest_card);

  msg.type = MESSAGE_TYPE_BAT;
  msg.source = MACHINE_TAG;
  msg.dest = 0;
  sendto(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.sndaddr, sizeof(struct sockaddr_in));

  #endif

  while(1) {
    recvfrom(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.recaddr, &recvlen);

    if(msg.type == MESSAGE_TYPE_BAT || (msg.sequence == current_seq && !(msg.read_flags & READ_FLAG))) {
      msg.read_flags |= READ_FLAG;

      if(msg.dest == 0 || msg.dest == MACHINE_TAG) {
        switch(msg.type) {
          case MESSAGE_TYPE_BAT:
            if(msg.source == MACHINE_TAG) {
              msg.dest = winner;
              highest_card = NO_CARD;
              fprintf(stdout, "Jogador %c venceu esta rodada!\n", winner);

              send_message(&condata, 0, 0, MESSAGE_TYPE_RESET_WINNER);
            }

            if(msg.dest == 0) {
              play_card(&condata, my_cards, faced_card, &winner, &highest_card);
            } else if(msg.dest == MACHINE_TAG) {
              msg.source = msg.dest;
              msg.dest = 0;

              play_card(&condata, my_cards, faced_card, &winner, &highest_card);
            }

            sendto(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.sndaddr, sizeof(struct sockaddr_in));
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

            process_play(msg.source, msg.card, faced_card, &winner, &highest_card);
            break;

          case MESSAGE_TYPE_RESET_WINNER:
            highest_card = NO_CARD;
            fprintf(stdout, "Jogador %c venceu esta rodada!\n", winner);
            break;
        }
      }

      if(msg.type != MESSAGE_TYPE_BAT) {
        current_seq = (current_seq + 1) % 0xFF;
      }
    }

    sendto(condata.sock, &msg, sizeof(struct message), 0, (struct sockaddr *) &condata.sndaddr, sizeof(struct sockaddr_in));
  }

  return 0;
}
