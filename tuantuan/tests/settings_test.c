// Host regression for untrusted camera QR payloads. No real credentials.
#include "companion_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void parse(const char *text,bool expected)
{
    companion_settings_t result;
    bool actual=companion_settings_parse_qr(text,strlen(text),&result);
    assert(actual==expected);
    if (!actual) assert(result.kind==0 && result.api_key[0]==0);
}
int main(void)
{
    parse("sk-dummy-test-credential-only",true);
    parse("sk-short",false);
    parse("sk-dummy-test credential-only",false);
    parse("{\"v\":1,\"kind\":\"tuantuan.api-key\",\"key\":\"sk-dummy-test-credential-only\"}",true);
    parse("{\"v\":1,\"kind\":\"tuantuan.api-key\",\"key\":\"sk-dummy-test-credential-only\\u0000extra\"}",false);
    parse("{\"v\":2,\"kind\":\"tuantuan.api-key\",\"key\":\"sk-dummy-test-credential-only\"}",false);
    parse("{\"v\":1,\"kind\":\"other\",\"key\":\"sk-dummy-test-credential-only\"}",false);
    parse("{\"v\":1,\"kind\":\"tuantuan.api-key\",\"key\":\"sk-dummy-test-credential-only\"} trailing",false);
    parse("{\"v\":1,\"kind\":\"tuantuan.connection\",\"ssid\":\"Home\",\"password\":\"12345678\",\"url\":\"http://192.168.1.10:8765\",\"token\":\"dummy-device-token\"}",true);
    companion_settings_t s={.kind=COMPANION_SETTINGS_CONNECTION,.ssid="Home",.url="http:///missing-host",.token="dummy-device-token"};
    assert(!companion_settings_validate(&s,NULL,0));
    strcpy(s.url,"http://localhost:8765");assert(companion_settings_validate(&s,NULL,0));
    strcpy(s.password,"short");assert(!companion_settings_validate(&s,NULL,0));
    memset(s.password,'a',sizeof(s.password));assert(!companion_settings_validate(&s,NULL,0));
    char large[COMPANION_QR_BYTES];memset(large,'x',sizeof(large));
    assert(!companion_settings_parse_qr(large,sizeof(large),&s));
    assert(!companion_settings_parse_qr(NULL,1,&s));
    puts("settings: valid, malformed, truncated, oversized and masked credential checks passed");
}
