#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

#define DS_IMPL
#include "ds.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

typedef struct {
  i32 len; // id + data
  size_t read_len; // don't send to client
  u8 *data;
  Arena *al;
  double keep_alive_time;
} Packet;

typedef struct {
  int sock;
  enum {
    STATE_HANDSHAKE = 0,
    STATE_STATUS,
    STATE_LOGIN,
    STATE_TRANSFER,
    STATE_CONFIG,
    STATE_PLAY,
    NUM_STATES,
  } state;
  struct sockaddr_storage caddr;
  socklen_t caddr_len;
} Client;

char *state_strs[NUM_STATES] = {
  [STATE_HANDSHAKE] = "HANDSHAKE",
  [STATE_STATUS]    = "STATUS",
  [STATE_LOGIN]     = "LOGIN",
  [STATE_TRANSFER]  = "TRANSFER",
  [STATE_CONFIG]    = "CONFIG",
  [STATE_PLAY]      = "PLAY",
};

ssize_t recv_buf(Client *c, u8 *buf, ssize_t buf_len) {
  ssize_t recvd = 0;

  while (recvd < buf_len) {
    ssize_t bytes = recv(c->sock, buf + recvd, buf_len - recvd, 0);

    assert(bytes >= 1); // TODO: properly handle

    recvd += bytes;
  }

  return recvd;
}

// pass NULL to packet for recv
#define simple_read(buf, buf_len) \
do { \
  if (p != NULL) { \
    assert((size_t) p->read_len + (buf_len) <= (size_t) p->len); \
    memmove((buf), &p->data[p->read_len], (buf_len)); \
    p->read_len += buf_len; \
  } else recv_buf(c, (u8 *)(buf), (buf_len)); \
} while(0)

i32 read_var_int(Client *c, Packet *p) {
  i32 ret = 0;
  size_t bit_pos = 0;

  const int SEGMENT = 0x7F;
  const int CONTINUE = 0x80;

  do {
    u8 byte;
    simple_read(&byte, 1);

    ret |= (byte & SEGMENT) << bit_pos;
    if (!(byte & CONTINUE)) break;

    bit_pos += 7;
  } while (bit_pos < 32);

  return ret;
}

u8 *read_n_bytes(Arena *al, Client *c, Packet *p, size_t n) {
  u8 *ret = arena_alloc(al, n);
  simple_read(ret, n);
  return ret;
}

u8 read_byte(Client *c, Packet *p) {
  u8 ret = 0;
  simple_read(&ret, 1);
  return ret;
}

u8 *read_str(Arena *al, Client *c, Packet *p) {
  i32 ret_len = read_var_int(c, p);
  if (ret_len == 0) return NULL;
  u8 *ret = arena_alloc(al, ret_len + 1);
  simple_read(ret, ret_len);
  ret[ret_len] = 0;
  return ret;
}

u16 read_u16(Client *c, Packet *p) {
  u16 ret = 0;
  simple_read(&ret, 2);
  return ntohs(ret);
}

bool read_bool(Client *c, Packet *p) {
  return read_u16(c, p);
}

Packet recv_packet(Arena *al, Client *c) {
  Packet p = {0};
  p.len = read_var_int(c, NULL);
  p.data = arena_alloc(al, p.len);
  recv_buf(c, p.data, p.len);
  return p;
}

#define simple_write(buf, buf_len) \
do { \
  memmove(&p->data[p->len], (buf), (buf_len)); \
  p->len += buf_len; \
} while(0)

#define VAR_INT_MAX_LEN 5
// write to packet unless buf is not null
size_t write_var_int(Packet *p, u8 *buf, i32 val) {
  u32 uval = ((u32) val) & 0xffffffff;
  size_t i = 0;

  const int SEGMENT = 0x7F;
  const int CONTINUE = 0x80;

  while (uval & ~SEGMENT) {
    u8 byte = (uval & SEGMENT) | CONTINUE;
    if (buf == NULL) simple_write(&byte, 1);
    else buf[i] = byte;
    uval >>= 7;
    i += 1;
  }

  if (buf == NULL) simple_write(&uval, 1);
  else buf[i] = uval;

  i += 1;

  return i;
}

void write_n_bytes(Packet *p, u8 *buf, size_t n) {
  simple_write(buf, n);
}

void write_byte(Packet *p, u8 byte) {
  simple_write(&byte, 1);
}

// null-terminated unless n >= 0
// Don't include length
void write_str(Packet *p, u8 *str, ssize_t n) {
  size_t len = n >= 0 ? n : strlen(str);
  write_var_int(p, NULL, len);
  simple_write(str, len);
}

Packet begin_packet(Arena *al) {
  Packet p = {0};
  p.al = al;
  arena_alloc(al, VAR_INT_MAX_LEN);
  p.data = arena_alloc(al, 0);
  return p;
}

void end_packet(Packet *p) {
  int vlen = write_var_int(p, p->data - VAR_INT_MAX_LEN, p->len);
  memmove(p->data - VAR_INT_MAX_LEN + vlen, p->data, p->len);
  p->data = p->data - VAR_INT_MAX_LEN + vlen;
  p->al->pos += p->len;
  printf("Packet constructed!\n");
}

ssize_t send_packets(Client *c, Arena *to_send) {
  ssize_t sent = 0;

  while (sent < to_send->pos) {
    ssize_t bytes = send(c->sock, &to_send->buf[sent], to_send->pos - sent, 0);
    assert(bytes >= 0);
    sent += bytes;
  }

  to_send->pos -= sent;
  assert(to_send->pos >= 0);

  return sent;
}

int main() {
  u16 port = 25565;

  const int server_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  {
    int yes = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  }

  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(port),
  };

  if (bind(server_sock, (struct sockaddr *) &addr, sizeof(addr))) {
    perror("Bind error");
    exit(EXIT_FAILURE);
  }

  socklen_t addr_len = sizeof(addr);
  getsockname(server_sock, (struct sockaddr *) &addr, &addr_len);
  printf("Server is on port %d\n", ntohs(addr.sin_port));

  if (listen(server_sock, 1)) {
    perror("Listen error");
    exit(EXIT_FAILURE);
  }

  Client client = {0};
  client.caddr_len = sizeof(client.caddr);
  client.sock = accept(
    server_sock,
    (struct sockaddr *) &client.caddr,
    &client.caddr_len
  );

  Arena scratch = arena_init(256 * 1024 * 1024);
  Arena to_send = arena_init(256 * 1024 * 1024);

  while (true) {
    char buf[1];
    ssize_t status = recv(client.sock, buf, 1, MSG_PEEK | MSG_DONTWAIT);
    if (status == 0) continue;

    Packet p = recv_packet(&scratch, &client);
    i32 pid = read_var_int(&client, &p);
    printf("Packet length: %d\n", p.len);
    printf("ID: %d\n", pid);
    printf("client.state: %s\n", state_strs[client.state]);

    switch(client.state) {
      case STATE_HANDSHAKE: {
        switch (pid) {
        case 0: {
          i32 r_version = read_var_int(&client, &p);
          u8 *r_addr_str = read_str(&scratch, &client, &p);
          u16 r_port = read_u16(&client, &p);
          client.state = read_var_int(&client, &p);

          printf("Version: %d\n", r_version);
          printf("Address: %s\n", r_addr_str);
          printf("Port: %d\n", r_port);
          printf("Next state: %d\n", client.state);
        } break;

        default: assert(0 && "Unhandled");
        }
      } break;

      case STATE_LOGIN: {
        switch(pid) {
        case 0: { // Login start
          u8 *username = read_str(&scratch, &client, &p);
          u32 *uuid = (u32 *) read_n_bytes(&scratch, &client, &p, 16);

          printf("Username: %s\n", username);
          printf("UUID: %x%x%x%x\n",
            ntohl(uuid[0]),
            ntohl(uuid[1]),
            ntohl(uuid[2]),
            ntohl(uuid[3])
          );

          // Login success
          Packet success = begin_packet(&to_send);
            write_var_int(&success, NULL, 2);
            write_n_bytes(&success, (u8 *) uuid, 16);
            write_str(&success, username, -1);
            write_var_int(&success, NULL, 0); // no properties
            write_byte(&success, true); // no strict error handling
          end_packet(&success);
        } break;

        case 3: { // Login acknowledged
          client.state = STATE_CONFIG;

          // Finish configuration
          Packet finish = begin_packet(&to_send);
            write_var_int(&finish, NULL, 3);
          end_packet(&finish);
        } break;

        default: assert(0 && "Unhandled");
        }
      } break;

      case STATE_CONFIG: {
        switch (pid) {
          case 0: {
            u8 *locale = read_str(&scratch, &client, &p);
            u8 view_dist = read_byte(&client, &p);
            u32 chat_mode = read_var_int(&client, &p);
            bool chat_colors = read_bool(&client, &p);
            i8 skin_parts = read_byte(&client, &p);
            u32 main_hand = read_var_int(&client, &p);
            // are these unused?
            // bool enable_text_filtering = read_bool(&client, &p);
            // bool allow_server_listings = read_bool(&client, &p);

            printf("Locale: %s\n", locale);
            printf("View dist: %d\n", view_dist);
            printf("Chat mode: %d\n", chat_mode);
            printf("Chat colors: %d\n", chat_colors);
            printf("Skin parts (Bit mask): %d\n", skin_parts);
            printf("Main hand: %d\n", main_hand);
            // printf("Enable text filtering: %d\n", enable_text_filtering);
            // printf("Allow server listings: %d\n", allow_server_listings);
          } break;

          case 2: { // Serverbound plugin message
            // TODO: handle STATE_CONFIG case 2
            printf("Note: unhandled\n");
          } break;

          case 3: { // Acknowledge finish configuration
            client.state = STATE_PLAY;
          } break;

          default: assert(0 && "Unhandled");
        }
      } break;

      default: assert(0 && "Unhandled");
    }

    send_packets(&client, &to_send);
    arena_free_all(&scratch);
    printf("-------------\n");
  }

  close(client.sock);
  close(server_sock);

  return 0;
}
