#include "dns_server.h"
#include <arpa/inet.h>
#include <string.h>
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <sys/param.h>
#include "esp_system.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define DNS_PORT (53)
#define DNS_MAX_LEN (128)  
#define DNS_NAME_MAX_LEN (32)  // Maximum length for domain names

#define OPCODE_MASK (0x7800)
#define QR_FLAG (1 << 7)
#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (60) 

static const char *DNS_TAG = "DNS";

typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

typedef struct {
    uint16_t type;
    uint16_t class;
} dns_question_t;

typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

static inline char *parse_dns_name(char *raw_name, char *parsed_name, size_t parsed_name_max_len)  // Made inline for performance
{
    char *label = raw_name;
    char *name_itr = parsed_name;
    int name_len = 0;

    do {
        int sub_name_len = *label;

        name_len += (sub_name_len + 1);
        if (name_len > parsed_name_max_len) {
            return NULL;
        }

        memcpy(name_itr, label + 1, sub_name_len);
        name_itr[sub_name_len] = '.';
        name_itr += (sub_name_len + 1);
        label += sub_name_len + 1;
    } while (*label != 0);

    parsed_name[name_len - 1] = '\0';
    return label + 1;
}

static int parse_dns_request(char *req, size_t req_len, char *dns_reply, size_t dns_reply_max_len)
{
    if (req_len > dns_reply_max_len) {
        return -1;
    }

    // Prepare the reply
    memset(dns_reply, 0, dns_reply_max_len);
    memcpy(dns_reply, req, req_len);

    // Endianess of NW packet different from chip
    dns_header_t *header = (dns_header_t *)dns_reply;

    // Not a standard query
    if ((header->flags & OPCODE_MASK) != 0) {
        return 0;
    }

    // Set question response flag
    header->flags |= QR_FLAG;

    uint16_t qd_count = ntohs(header->qd_count);
    header->an_count = htons(qd_count);

    int reply_len = qd_count * sizeof(dns_answer_t) + req_len;
    if (reply_len > dns_reply_max_len) {
        return -1;
    }

    // Pointer to current answer and question
    char *cur_ans_ptr = dns_reply + req_len;
    char *cur_qd_ptr = dns_reply + sizeof(dns_header_t);
    char name[DNS_NAME_MAX_LEN];  // Using defined max length

    // Respond to all questions with the ESP32's IP address
    for (int i = 0; i < qd_count; i++) {
        char *name_end_ptr = parse_dns_name(cur_qd_ptr, name, sizeof(name));
        if (name_end_ptr == NULL) {
            return -1;
        }

        dns_question_t *question = (dns_question_t *)(name_end_ptr);
        uint16_t qd_type = ntohs(question->type);
        uint16_t qd_class = ntohs(question->class);

        if (qd_type == QD_TYPE_A) {
            dns_answer_t *answer = (dns_answer_t *)cur_ans_ptr;

            answer->ptr_offset = htons(0xC000 | (cur_qd_ptr - dns_reply));
            answer->type = htons(qd_type);
            answer->class = htons(qd_class);
            answer->ttl = htonl(ANS_TTL_SEC);

            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

            struct in_addr ip_addr;
            ip_addr.s_addr = ip_info.ip.addr;

            answer->addr_len = htons(sizeof(ip_info.ip.addr));
            answer->ip_addr = ip_info.ip.addr;
        }
    }
    return reply_len;
}

static void dns_server_task(void *pvParameters)
{

    char rx_buffer[DNS_MAX_LEN];  // Using defined max length
    char addr_str[DNS_NAME_MAX_LEN];  // Using defined max length
    int addr_family;
    int ip_protocol;

    while (1) {
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(DNS_PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(DNS_TAG, "Unable to create socket: errno %d", errno);
            break;
        }

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(DNS_TAG, "Socket unable to bind: errno %d", errno);
        }

        while (1) {
            struct sockaddr_in source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(DNS_TAG, "recvfrom failed: errno %d", errno);
                close(sock);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                inet_ntoa_r(source_addr.sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

                char reply[DNS_MAX_LEN];
                int reply_len = parse_dns_request(rx_buffer, len, reply, DNS_MAX_LEN);

                if (reply_len > 0) {
                    err = sendto(sock, reply, reply_len, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
                    if (err < 0) {
                        ESP_LOGE(DNS_TAG, "Error sending DNS response: errno %d", errno);
                        break;
                    }
                }
            }
            vTaskDelay(pdMS_TO_TICKS(32));
        }

        if (sock != -1) {
            ESP_LOGE(DNS_TAG, "Shutting down socket");
            shutdown(sock, 0);
            close(sock);
        }
        vTaskDelay(pdMS_TO_TICKS(32));
    }
    vTaskDelete(NULL);
}

void start_dns_server(TaskHandle_t *dns_task_handle)
{
    xTaskCreatePinnedToCore(&dns_server_task, "dns_server", 2400, NULL, 5, dns_task_handle, 1);
}
