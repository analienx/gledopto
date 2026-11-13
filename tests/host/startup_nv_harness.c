#include <stdio.h>
#include <setjmp.h>
#include "proj/tl_common.h"
#include "glsd301p_stack_nv.h"
/* Include the byte-identical pinned SDK body; only flash/allocator HALs are fake. */
#include "proj/drivers/drv_nv.c"
static uint8_t media[0x80000], snapshot[0x80000], rotation[0x80000];
static unsigned erases, ops, fail_at;
static bool cut_enabled;
static jmp_buf power_cut;
void flash_read(u32 a,u32 n,u8 *p){assert(a+n<=sizeof(media));memcpy(p,media+a,n);}
void flash_write(u32 a,u32 n,u8 *p){
    assert(a+n<=sizeof(media));++ops;
    for(u32 i=0;i<n;i++){
        media[a+i]&=p[i];
        if(cut_enabled && ops==fail_at && i==n/2)longjmp(power_cut,1);
    }
}
bool flash_writeWithCheck(u32 a,u32 n,u8 *p){flash_write(a,n,p);return memcmp(media+a,p,n)==0;}
void flash_erase(u32 a){
    assert(a%4096==0 && a+4096<=sizeof(media));++erases;++ops;
    if(cut_enabled && ops==fail_at){memset(media+a,0xff,2048);longjmp(power_cut,1);}
    memset(media+a,0xff,4096);
}
static bool marker_read(uint8_t *p){return nv_flashReadNew(1,NV_MODULE_APP,0xe5,8,p)==NV_SUCC;}
static bool marker_write(const uint8_t *p){return nv_flashWriteNew(1,NV_MODULE_APP,0xe5,8,(u8*)p)==NV_SUCC;}
static bool unexpected_erase(uint8_t n){assert(0 && "startup traffic invalidated network epoch");return false;}
static bool blank(uint8_t n){return true;}
static void intact(void){
    /* Compare every byte outside the two ZCL sectors, including keys/counters. */
    assert(memcmp(media,snapshot,0x3a000)==0);
    assert(memcmp(media+0x3c000,snapshot+0x3c000,sizeof(media)-0x3c000)==0);
    glsd301p_stack_nv_ops_t o={marker_read,marker_write,unexpected_erase,blank};
    assert(glsd301p_stack_nv_ensure_clean(&o,7,false)==GLSD301P_STACK_NV_CLEAN);
}
int main(void){
    uint8_t marker[8],value[8]={0},check[8],sentinel[16];
    memset(media,0xff,sizeof(media));memset(sentinel,0x6b,sizeof(sentinel));
    glsd301p_stack_nv_marker(marker);assert(marker_write(marker));
    assert(nv_flashWriteNew(1,NV_MODULE_ZB_INFO,1,sizeof(sentinel),sentinel)==NV_SUCC);
    assert(nv_flashWriteNew(1,NV_MODULE_KEYPAIR,1,sizeof(sentinel),sentinel)==NV_SUCC);
    memcpy(snapshot,media,sizeof(media));unsigned start=erases;bool captured=false;
    for(unsigned i=0;i<400;i++){
        value[0]=(uint8_t)i;value[1]=(uint8_t)(i>>8);
        unsigned prior=erases;memcpy(rotation,media,sizeof(media));
        assert(nv_flashWriteNew(1,NV_MODULE_ZCL,0xe6,sizeof(value),value)==NV_SUCC);
        assert(nv_flashReadNew(1,NV_MODULE_ZCL,0xe6,sizeof(check),check)==NV_SUCC);
        assert(memcmp(check,value,sizeof(check))==0);intact();
        if(i>5 && erases>prior && !captured){
            /* Exercise cuts inside real sector-copy writes and erases. */
            uint8_t completed[sizeof(media)];memcpy(completed,media,sizeof(media));
            for(unsigned failure=1;failure<=50;failure++){
                memcpy(media,rotation,sizeof(media));ops=0;fail_at=failure;cut_enabled=true;
                if(setjmp(power_cut)==0)(void)nv_flashWriteNew(1,NV_MODULE_ZCL,0xe6,sizeof(value),value);
                cut_enabled=false;intact();
                (void)nv_flashReadNew(1,NV_MODULE_ZCL,0xe6,sizeof(check),check);
                /* Recovery may lose ZCL state; it must not touch APP/network. */
                (void)nv_flashWriteNew(1,NV_MODULE_ZCL,0xe6,sizeof(value),value);intact();
            }
            memcpy(media,completed,sizeof(media));captured=true;
        }
    }
    assert(captured && erases>start+5);
    memset(media+0x3a000,0,4);memset(media+0x3b000,0,4);
    (void)nv_flashWriteNew(1,NV_MODULE_ZCL,0xe6,sizeof(value),value);intact();
    puts("SDK_NV_ISOLATION=PASS 400 writes, repeated rotation, 50 cuts, invalid sector headers; APP/network unchanged");
    return 0;
}
