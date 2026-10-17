// Host camera-frame equivalent: decode generated grayscale QR images.
#include "quirc.h"
#include "companion_settings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    assert(argc==3);
    FILE *file=fopen(argv[1],"rb");assert(file);
    int w,h,max;assert(fscanf(file,"P5 %d %d %d",&w,&h,&max)==3 && max==255);fgetc(file);
    struct quirc *q=quirc_new();assert(q && quirc_resize(q,w,h)==0);
    unsigned char *image=quirc_begin(q,NULL,NULL);assert(fread(image,1,w*h,file)==(size_t)(w*h));fclose(file);quirc_end(q);
    struct quirc_code *code=calloc(1,sizeof(*code));struct quirc_data *data=calloc(1,sizeof(*data));
    companion_settings_t *settings=calloc(1,sizeof(*settings));assert(code && data && settings);
    int found=0;
    for(int i=0;i<quirc_count(q);i++) {
        quirc_extract(q,i,code);
        if(quirc_decode(code,data)==QUIRC_SUCCESS && companion_settings_parse_qr((char*)data->payload,data->payload_len,settings)) found++;
    }
    assert(found==atoi(argv[2]));
    free(code);free(data);free(settings);quirc_destroy(q);puts("QR frame decode passed");
}
