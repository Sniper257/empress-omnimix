from pathlib import Path
import shutil
HERE=Path(__file__).resolve().parents[1]/"tools/offdisc"
def instrument(src):
    def edit(name,old,new,first=False):
        p=src/'src'/name;s=p.read_text()
        if (not first and s.count(old)!=1) or old not in s:raise ValueError((name,old,s.count(old)))
        p.write_text(s.replace(old,new,1))
    edit('mmceman.c','    printf("Multipurpose', '    megamix_diag(2,0,0);\n    printf("Multipurpose')
    edit('mmceman.c','    //Install hooks','    megamix_diag(3,0,0);\n    //Install hooks\n    megamix_diag(10,0,0);')
    edit('mmceman.c','    if (rv != 0) {','    megamix_diag(11,rv,0);\n    if (rv != 0) {')
    edit('mmceman.c','    mmce_init(2);\n    mmce_init(3);',
         '    megamix_diag(12,2,0);mmce_init(2);megamix_diag(13,2,0);\n    megamix_diag(14,3,0);mmce_init(3);megamix_diag(15,3,0);')
    edit('mmceman.c','    mmce_fs_register();','    megamix_diag(16,0,0);\n    mmce_fs_register();\n    megamix_diag(17,0,0);')
    edit('mmceman.c','    if (mi && ((mi->newflags & 2) != 0))',
         '    megamix_diag(18,(unsigned)mi,(unsigned)startaddr);\n    if (mi && ((mi->newflags & 2) != 0))')
    edit('mmceman.c','    return MODULE_RESIDENT_END;','    megamix_diag(19,0,0);megamix_diag_end();\n    return MODULE_RESIDENT_END;')
    edit('mmceman.c','    if (argc >= 0) \n',
         '    megamix_diag_bind(argc,argv);megamix_diag(1,argc,(unsigned)mi);\n    if (argc >= 0) \n')
    edit('mmceman.c','        res = mmce_cmd_ping();',
         '        megamix_diag(60,port,i);res = mmce_cmd_ping();megamix_diag(61,port,res);')
    edit('mmce_sio2.c','    rv = sio2man_hook_init();','    megamix_diag(20,0,0);rv = sio2man_hook_init();megamix_diag(21,rv,0);')
    edit('mmce_sio2.c','\tevent_flag = CreateEventFlag(&event);',
         '    megamix_diag(22,0,0);\n\tevent_flag = CreateEventFlag(&event);\n    megamix_diag(23,event_flag,0);')
    edit('mmce_sio2.c','    //Ensure IOP DMA channels are enabled',
         '    megamix_diag(24,0,0);\n    //Ensure IOP DMA channels are enabled')
    edit('mmce_sio2.c','    EnableIntr(IOP_IRQ_SIO2);','    EnableIntr(IOP_IRQ_SIO2);\n    megamix_diag(25,0,0);')
    edit('mmce_sio2.c','    //Start transfer\n    inl_sio2_ctrl_set(0x3a1);',
         '    //Start transfer\n    megamix_diag(50,tx_size,rx_size);\n    inl_sio2_ctrl_set(0x3a1);')
    edit('mmce_sio2.c','    SetAlarm(timeout, mmce_sio2_timeout_handler, &event_flag);',
         '    megamix_diag(51,event_flag,timeout->lo);\n    int alarm_result=SetAlarm(timeout, mmce_sio2_timeout_handler, &event_flag);\n    megamix_diag(52,event_flag,alarm_result);')
    edit('mmce_sio2.c','    WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);',
         '    int wait_result=WaitEventFlag(event_flag, EF_SIO2_TRANSFER_TIMEOUT | EF_SIO2_INTR_COMPLETE, 1, &resbits);\n    megamix_diag(53,resbits,wait_result);',first=True)
    edit('sio2man_hook.c','        intrman_internals = GetIntrmanInternalData();',
         '        megamix_diag(30,0,0);\n        intrman_internals = GetIntrmanInternalData();\n        megamix_diag(31,(unsigned)intrman_internals,0);')
    edit('sio2man_hook.c','    // Hook into \'sio2man\' now if it\'s already loaded',
         '    megamix_diag(32,0,0);\n    // Hook into \'sio2man\' now if it\'s already loaded')
    edit('sio2man_hook.c','static void _sio2man_hook(iop_library_t *lib)\n{',
         'static void _sio2man_hook(iop_library_t *lib)\n{\n    megamix_diag(35,(unsigned)lib,lib->version);')
    edit('ioplib.c','    while (libptr != NULL) {',
         '    while (libptr != NULL) {\n        megamix_diag(34,(unsigned)libptr,count);')
    edit('mmce_cmds.c','    mmce_sio2_lock();',
         '    megamix_diag(40,0,0);mmce_sio2_lock();megamix_diag(41,0,0);',first=True)
    edit('mmce_cmds.c','    res = mmce_sio2_tx_rx_pio(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf, &timeout_200ms);',
         '    megamix_diag(42,0,0);res = mmce_sio2_tx_rx_pio(sizeof(wrbuf), sizeof(rdbuf), wrbuf, rdbuf, &timeout_200ms);megamix_diag(43,res,0);')
    edit('mmce_cmds.c','    mmce_sio2_unlock();','    mmce_sio2_unlock();megamix_diag(44,0,0);',first=True)
    shutil.copy2(HERE/'mmce_boot_diag_iop.h',src/'include/megamix_diag.h')
    shutil.copy2(HERE/'mmce_boot_diag_iop.c',src/'src/megamix_diag.c')
    p=src/'Makefile';s=p.read_text();s=s.replace('all: $(IOP_BIN)',
        'IOP_OBJS += megamix_diag.o\nIOP_CFLAGS += -include include/megamix_diag.h\n\nall: $(IOP_BIN)');p.write_text(s)

def patch(src):
    original=src.parent/"mmceman-original"
    shutil.copytree(src,original)
    instrument(src)
    # R3 keeps breadcrumbs on startup paths only. Restore the original
    # per-transfer/command functions so PLAY gets no diagnostic branches.
    (src/'src/mmce_cmds.c').write_bytes((original/'src/mmce_cmds.c').read_bytes())
    p=src/'src/mmce_sio2.c';s=p.read_text();original=(original/'src/mmce_sio2.c').read_text()
    a=s.index('int mmce_sio2_tx_rx_pio(');b=s.index('\n//Simplified read',a)
    oa=original.index('int mmce_sio2_tx_rx_pio(');ob=original.index('\n//Simplified read',oa)
    p.write_text(s[:a]+original[oa:ob]+s[b:])
    p=src/'src/mmce_fs.c';s=p.read_text().replace('    iomanX_DelDrv(mmce_dev.name);',
         '    megamix_diag(70,0,0);int del_result=iomanX_DelDrv(mmce_dev.name);megamix_diag(71,del_result,0);',1)
    s=s.replace('    if (iomanX_AddDrv(&mmce_dev)!= 0)',
         '    megamix_diag(72,0,0);int add_result=iomanX_AddDrv(&mmce_dev);megamix_diag(73,add_result,0);\n    if (add_result!=0)',1)
    s=s.replace('    memset(mmce_fs_fds, -1, sizeof(mmce_fs_fds));',
         '    megamix_diag(74,0,0);memset(mmce_fs_fds,-1,sizeof(mmce_fs_fds));megamix_diag(75,0,0);',1);p.write_text(s)
