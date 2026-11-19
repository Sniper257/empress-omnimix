#ifndef MEGAMIX_SAVE_PROFILE_LAUNCHER_H
#define MEGAMIX_SAVE_PROFILE_LAUNCHER_H
#ifndef HOST_TEST
#include <fcntl.h>
#include <unistd.h>
#endif
/* One exclusive, small receipt only for the explicit profiler opt-in. Separate
 * from verbose boot logging; records the actual post-cache observer binding. */
static int save_profile_launch_receipt(void)
{
    if(!debug_enabled || !save_profile_enabled)return 0;
    if(!save_probe_address)return -1;
    const sp_control *p=(const sp_control *)(unsigned long)save_probe_address;
    char text[384],path[160];
    int bytes=snprintf(text,sizeof text,"MMIX_SAVE_LAUNCH 1\nrevision R60\naddress %08x\nenabled %u\npath %s\ncalibration %u %u %u %u %u %u\n",
        save_probe_address,p->enabled,p->path,p->count_lo,p->count_hi,p->bus_lo,p->bus_hi,p->samples,p->bus_hz);
    if(bytes<=0 || (unsigned)bytes>=sizeof text || !p->enabled || !p->path[0])return -2;
    for(unsigned i=1;i<=9999;i++){
        snprintf(path,sizeof path,"%slogs/save-launch-%04u.txt",DEV,i);
        FILE *probe=fopen(path,"rb");if(probe){fclose(probe);continue;}
#ifdef HOST_TEST
        FILE *out=fopen(path,"wx");if(!out)return -3;
        int ok=fwrite(text,1,(unsigned)bytes,out)==(unsigned)bytes;
        if(fclose(out))ok=0;return ok?(int)i:-4;
#else
        int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0666);if(fd<0)return -3;
        unsigned done=0;int ok=1;
        while(done<(unsigned)bytes){int n=write(fd,text+done,(unsigned)bytes-done);if(n<=0){ok=0;break;}done+=(unsigned)n;}
        if(close(fd)<0)ok=0;return ok?(int)i:-4;
#endif
    }
    return -5;
}
#endif
