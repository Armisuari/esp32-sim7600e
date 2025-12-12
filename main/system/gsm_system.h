#ifndef GSM_SYSTEM_H
#define GSM_SYSTEM_H

void gsm_modem_check();
bool gsm_sim_check();
void gsm_turnoff_echo();
void gsm_wait_for_network_and_time();
bool gsm_enable_internet(const char *apn);
void gsm_get_imei(void);


#endif // GSM_H