// SPDX-License-Identifier: Apache-2.0
#include "companion_pcm.h"
static int sample(const uint8_t *p)
{return (int16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static void write_sample(uint8_t *p,int value)
{
    /* Match the existing +6dB preset for generic providers, with saturation. */
    value*=2;if(value>32767)value=32767;if(value<-32768)value=-32768;
    p[0]=(uint8_t)value;p[1]=(uint8_t)((unsigned)value>>8);
}
size_t companion_pcm_convert(uint8_t *pcm,size_t bytes,unsigned rate,size_t *consumed)
{
    if(rate==16000){*consumed=bytes&~(size_t)1;return *consumed;}
    if(rate!=24000){*consumed=0;return 0;}
    *consumed=bytes-bytes%6;
    size_t out=0;
    for(size_t i=0;i<*consumed;i+=6){
        int a=sample(pcm+i),b=sample(pcm+i+2),c=sample(pcm+i+4);
        write_sample(pcm+out,a);write_sample(pcm+out+2,(b+c)/2);out+=4;
    }
    return out;
}
