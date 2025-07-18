#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "proto/c_string.h"
#include "proto/parrot_message.h"
#include "proto/parrot_payload.h"

static const char *host = "";
static uint16_t port = 18029;
static int sock = -1;
static parrot_bool is_logged_in = parrot_false;
static const uint32_t device_id = 0xC1C2C3C4;
static uint16_t serial = 0;

static int audio_frame_count = 0;
static time_t last_keep_alive_time = 0;
static volatile uint8_t exit_flag = 0;
static int exit_value = 0;

int create_udp_socket(int local_port);
int connect_udp_socket();
void send_register_request();
void read_udp_messages();
void routine_check();

void on_interrupt(const int sig) {
    (void)sig;

    exit_flag = 1;
}


int main(const int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <host> [port]\n", argv[0]);
        return 1;
    }

    signal(SIGINT, on_interrupt);
    signal(SIGPIPE, SIG_IGN);

    host = argv[1];
    if (argc > 2) port = (uint16_t) strtol(argv[2], NULL, 10);

    // listen on udp
    sock = create_udp_socket(9802);
    if (sock < 0) {
        fprintf(stderr, "Failed to create UDP socket\n");
        return 1;
    }

    int n = connect_udp_socket();
    if (n < 0) {
        close(sock);
        fprintf(stderr, "Failed to connect UDP socket\n");
        return 1;
    }

    // event loop
    time_t last_check_time = time(NULL);
    send_register_request();
    while (!exit_flag) {
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        n = select(sock + 1, &read_fds, NULL, NULL, &timeout);
        if (n > 0) {
            read_udp_messages();
        }

        const time_t now = time(NULL);
        if (now > last_check_time) {
            routine_check(); // periodic status check
            last_check_time = now;
        }
    }

    return exit_value;
}

void send_keep_alive() {
    printf("send keep alive\n");
    parrot_message msg;
    memset(&msg, 0, sizeof(parrot_message));
    msg.command = 0x03; // Keep-alive request
    msg.device = device_id;
    msg.serial = ++serial;

    char buf[512];
    const uint16_t n = parrot_message_serialize(buf, sizeof(buf), &msg, parrot_true);
    const ssize_t ret = send(sock, buf, n, 0);
    if (ret < 0) {
        perror("send");
    }
}

void routine_check() {
    if (!is_logged_in) {
        send_register_request();
    } else {
        const time_t now = time(NULL);
        if (now > last_keep_alive_time + 30) {
            last_keep_alive_time = now;
            send_keep_alive();
        }
    }

    if (audio_frame_count != 0) {
        printf("audio frame count %d\n", audio_frame_count);
        audio_frame_count = 0;
    }
}


static void on_audio_notify(const void *payload, const uint16_t len) {
    payload_parse parse;
    parrot_payload_parse_init(&parse, payload, len);

    payload_entry entry;

    while (parse.pos < parse.length) {
        if (!parrot_payload_parse_entry(&entry, &parse))
            break;

        if (entry.key == 1 && entry.is_string) {
            printf("opus frame: %d bytes\n", entry.value.str.length);
        }
    }
}

static void on_volume_notify(const void *payload, const uint16_t len) {
    payload_parse parse;
    parrot_payload_parse_init(&parse, payload, len);

    payload_entry entry;

    int audio_dev_id = 0;
    int volume = 0;


    while (parse.pos < parse.length) {
        if (!parrot_payload_parse_entry(&entry, &parse))
            break;

        if (entry.key == 1) audio_dev_id = (int) entry.value.i64;
        if (entry.key == 2) volume = (int) entry.value.i64;
    }

    printf("volume notify id=%d value=%d\n", audio_dev_id, volume);
}

static void on_status_notify(const void *payload, const uint16_t len) {
    payload_parse parse;
    parrot_payload_parse_init(&parse, payload, len);

    payload_entry entry;

    int code = 0;
    const char *message_data = "";
    uint16_t message_len = 0;

    while (parse.pos < parse.length) {
        if (!parrot_payload_parse_entry(&entry, &parse))
            break;

        if (entry.key == 1) {
            code = (int) entry.value.i64;
        } else if (entry.key == 2) {
            message_data = entry.value.str.data;
            message_len = entry.value.str.length;
        }
    }

    printf("online status=%d message=%.*s\n", code, message_len, message_data);
}

static void on_register_res(const void *payload, const uint16_t len) {
    payload_parse parse;
    parrot_payload_parse_init(&parse, payload, len);

    payload_entry entry;

    int code = 0;
    const char *message_data = "";
    uint16_t message_len = 0;

    while (parse.pos < parse.length) {
        if (!parrot_payload_parse_entry(&entry, &parse))
            break;

        if (entry.key == 1) {
            code = (int) entry.value.i64;
        } else if (entry.key == 2) {
            message_data = entry.value.str.data;
            message_len = entry.value.str.length;
        }
    }

    printf("Register status=%d message=%.*s\n", code, message_len, message_data);
    is_logged_in = parrot_true;
}

static void handle_udp_message(const void *data, const int length) {
    parrot_message msg;
    const parrot_bool ok = parrot_message_parse(&msg, data, length);
    if (!ok) {
        fprintf(stderr, "corrupted udp message.\n");
        return;
    }

    switch (msg.command) {
        case 0x02: // Register response
            on_register_res(msg.payload_data, msg.payload_len);
            break;
        case 0x04:
            printf("keep-alive response received\n");
            break;
        case 0x40:
            on_status_notify(msg.payload_data, msg.payload_len);
            break;
        case 0x41:
            on_audio_notify(msg.payload_data, msg.payload_len);
            break;
        case 0x42:
            printf("start play notify\n");
            break;
        case 0x43:
            printf("stop play notify\n");
            break;
        case 0x44:
            on_volume_notify(msg.payload_data, msg.payload_len);
            break;
        default:
            break;
    }
}

void read_udp_messages() {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    char read_buf[1500];
    while (1) {
        const int n = (int) recvfrom(sock, read_buf, sizeof(read_buf), 0,
                                     (struct sockaddr*) &addr,
                                     &addr_len);
        if (n > 0) {
            handle_udp_message(read_buf, n);
            continue;
        }

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }

            if (errno != EAGAIN) {
                fprintf(stderr, "recvfrom: %s\n", strerror(errno));
            }
            return;
        }
    }
}

void send_register_request() {
    c_string payload;
    memset(&payload, 0, sizeof(payload));
    // field #1 client_ip       (string)
    // field #2 client_version  (string)
    // field #3 ao_volume       (integer)
    parrot_payload_put_string(&payload, 1, "192.168.124.130",  -1);
    parrot_payload_put_string(&payload, 2, "1.0.1", -1);
    parrot_payload_put_integer(&payload, 3, 100);

    printf("send register request\n");
    parrot_message msg;
    memset(&msg, 0, sizeof(parrot_message));
    msg.command = 0x01; // Register request
    msg.device = device_id;
    msg.serial = ++serial;
    msg.payload_data = payload.data;
    msg.payload_len = payload.length;


    char buf[512];
    const uint16_t n = parrot_message_serialize(buf, sizeof(buf), &msg, parrot_true);
    const ssize_t ret = send(sock, buf, n, 0);
    if (ret < 0) {
        perror("send");
    }

    c_string_hard_clear(&payload);
}

static int ensure_nonblock(const int fd) {
    uint32_t fl = fcntl(fd, F_GETFL, 0);
    if (fl == 0xFFFFFFFFu) {
        fprintf(stderr, "fcntl(%d, F_GETFL, 0): %s\n", fd, strerror(errno));
        return -1;
    }

    if ((fl & O_NONBLOCK) != 0) {
        return 0;
    }

    fl |= (uint32_t) O_NONBLOCK;

    const int ret = fcntl(fd, F_SETFL, fl);
    if (ret == -1) {
        fprintf(stderr, "fcntl(%d, F_SETFL, 0): %s", fd, strerror(errno));
        return -1;
    }

    return 0;
}

int connect_udp_socket(void) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_aton(host, &addr.sin_addr);

    const int n = connect(sock, (struct sockaddr*) &addr, sizeof(addr));
    if (n < 0) {
        perror("connect");
        close(sock);
        sock = -1;
        return -1;
    }

    return 0;
}

int create_udp_socket(const int local_port) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
        return sock;
    }

    // bind to local port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(local_port);
    if (0 != bind(sock, (struct sockaddr*) &addr, sizeof(addr))) {
        perror("bind");
        close(sock);
        sock = -1;
        return sock;
    }

    const int n = ensure_nonblock(sock);
    if (n != 0) {
        close(sock);
        sock = -1;
        return sock;
    }


    return sock;
}
