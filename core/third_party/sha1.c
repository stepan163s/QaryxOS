/* SHA1 implementation — public domain */
#include "sha1.h"
#include <string.h>

#define ROL32(v,n) (((v)<<(n))|((v)>>(32-(n))))

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t a,b,c,d,e,f,k,t,w[80];
    int i;
    for (i=0;i<16;i++)
        w[i]=(block[i*4]<<24)|(block[i*4+1]<<16)|(block[i*4+2]<<8)|block[i*4+3];
    for (i=16;i<80;i++)
        w[i]=ROL32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    a=state[0];b=state[1];c=state[2];d=state[3];e=state[4];
    for (i=0;i<80;i++) {
        if      (i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
        else if (i<40){f=b^c^d;          k=0x6ED9EBA1;}
        else if (i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
        else          {f=b^c^d;          k=0xCA62C1D6;}
        t=ROL32(a,5)+f+e+k+w[i];
        e=d;d=c;c=ROL32(b,30);b=a;a=t;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;
}

void SHA1Init(SHA1_CTX *ctx) {
    ctx->state[0]=0x67452301;ctx->state[1]=0xEFCDAB89;
    ctx->state[2]=0x98BADCFE;ctx->state[3]=0x10325476;ctx->state[4]=0xC3D2E1F0;
    ctx->count[0]=ctx->count[1]=0;
}

void SHA1Update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i, j = (ctx->count[0]>>3)&63;
    if ((ctx->count[0]+=(uint32_t)(len<<3))<(uint32_t)(len<<3)) ctx->count[1]++;
    ctx->count[1]+=(uint32_t)(len>>29);
    if (j+len>=64) {
        memcpy(&ctx->buffer[j],data,i=64-j);
        sha1_transform(ctx->state,ctx->buffer);
        for (;i+63<len;i+=64) sha1_transform(ctx->state,data+i);
        j=0;
    } else i=0;
    memcpy(&ctx->buffer[j],data+i,len-i);
}

void SHA1Final(uint8_t digest[20], SHA1_CTX *ctx) {
    uint8_t fin[8];
    int i;
    for (i=0;i<8;i++) fin[i]=(uint8_t)(ctx->count[(i<4)?1:0]>>(i<4?(24-i*8):(56-i*8)));
    uint8_t c=0x80;
    SHA1Update(ctx,&c,1);
    while ((ctx->count[0]>>3&63)!=56){c=0;SHA1Update(ctx,&c,1);}
    SHA1Update(ctx,fin,8);
    for (i=0;i<20;i++) digest[i]=(uint8_t)(ctx->state[i>>2]>>(24-(i&3)*8));
}

void sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    SHA1_CTX ctx; SHA1Init(&ctx); SHA1Update(&ctx,data,len); SHA1Final(digest,&ctx);
}

static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void base64_encode(const uint8_t *src, size_t len, char *dst) {
    size_t i=0,j=0;
    while (i<len) {
        uint32_t octet_a=i<len?src[i++]:0;
        uint32_t octet_b=i<len?src[i++]:0;
        uint32_t octet_c=i<len?src[i++]:0;
        uint32_t triple=(octet_a<<16)+(octet_b<<8)+octet_c;
        dst[j++]=B64[(triple>>18)&63];
        dst[j++]=B64[(triple>>12)&63];
        dst[j++]=B64[(triple>>6)&63];
        dst[j++]=B64[triple&63];
    }
    int mod=len%3;
    if (mod==1){dst[j-2]='=';dst[j-1]='=';}
    else if (mod==2){dst[j-1]='=';}
    dst[j]='\0';
}
