#ifndef GNSS_SYSTEM_H
#define GNSS_SYSTEM_H

typedef struct {
    double latitude;
    double longitude;
    double altitude;
    float speed;
    char timestamp[32]; // YYYY-MM-DD HH:MM:SS
} gps_info_t;

void gsm_enable_gnss(void);
void gnss_task(void *arg) ;

extern QueueHandle_t gnss_queue;
extern SemaphoreHandle_t gnss_ready_sem; // deklarasi extern

#endif // GNSS_H