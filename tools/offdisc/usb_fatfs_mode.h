/* Include after FatFs/io_common flag definitions. Preserve exclusive exports
 * even if a pathname appears between the read-open probe and creation. */
static unsigned char megamix_fatfs_mode(int flags)
{
    unsigned char mode=FA_OPEN_EXISTING;
    if(flags & O_RDONLY)mode|=FA_READ;
    if(flags & O_WRONLY)mode|=FA_WRITE;
    if((flags & (O_CREAT|O_EXCL))==(O_CREAT|O_EXCL))mode|=FA_CREATE_NEW;
    else if(flags & O_TRUNC)mode|=FA_CREATE_ALWAYS;
    else if(flags & O_APPEND)mode|=FA_OPEN_APPEND;
    else if(flags & O_CREAT)mode|=FA_OPEN_ALWAYS;
    return mode;
}
