#include "poom_handshake_crack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
int64_t esp_timer_get_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
int main(int argc, char **argv) {
    assert(argc == 4);
    const char *hs=argv[1];
    poom_hs_crack_result_t out;
    volatile bool cancel=false;
    assert(poom_hs_crack_run_file(hs, POOM_HS_SCOPE_QUICK, &cancel, NULL, NULL, &out)==ESP_OK);
    assert(out.found && !out.error && out.tried==1 && out.pmkid_count==1);
    assert(strcmp(out.password,"12345678")==0);
    assert(poom_hs_crack_run_file_wordlist(hs,argv[2],&cancel,NULL,NULL,&out)==ESP_OK);
    assert(out.found && !out.error && out.tried==2 && out.total==2);
    cancel=true;
    assert(poom_hs_crack_run_file(hs, POOM_HS_SCOPE_QUICK, &cancel, NULL, NULL, &out)==ESP_OK);
    assert(out.cancelled && !out.found && out.tried==0);
    assert(poom_hs_crack_run_file(argv[3],POOM_HS_SCOPE_QUICK,NULL,NULL,NULL,&out)==ESP_OK);
    assert(out.error && !out.found);
    assert(poom_hs_crack_run_file(NULL,POOM_HS_SCOPE_QUICK,NULL,NULL,NULL,&out)==ESP_ERR_INVALID_ARG);
    puts("PASSED: synthetic Python-oracle PMKID, built-in and streamed candidates, cancellation and malformed input");
}
