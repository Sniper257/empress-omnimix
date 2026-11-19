"""Apply the checked EMPRESS IOPRP310 layout adapter to a PRIVATE SDK copy."""
from pathlib import Path

HERE=Path(__file__).resolve().parent

def patch(sdk):
    directory=Path(sdk)/'iop/system/iomanx/src'
    p=directory/'ioman_sbv.c';s=p.read_text()
    if 'mmix_ioman_capture' in s:raise ValueError('IOMAN adapter already applied')
    anchor='static const char *ioman_modname = "IO/File_Manager";'
    assert s.count(anchor)==1
    s=s.replace(anchor,anchor+'\n#include "megamix_retail_ioman_layout.h"\nstatic mmix_ioman_snapshot retail_state;')
    anchor='    // patch the IOMAN export library table to call iomanX functions'
    assert s.count(anchor)==1
    s=s.replace(anchor,'    /* Reject unknown native layouts BEFORE changing imports or ownership. */\n'
                       '    if (mmix_ioman_capture(&info,&retail_state)) { ioman_exports=NULL;return -1; }\n\n'+anchor)
    begin=s.index('    /* steal the original IOMAN\'s 16 registered device entries */')
    end=s.index('\n\treturn 0;',begin)
    s=s[:begin]+'    mmix_ioman_takeover(&info,&retail_state,dev_list,file_table);\n'+s[end:]
    p.write_text(s)
    (directory/'megamix_retail_ioman_layout.h').write_bytes((HERE/'retail_ioman_layout.h').read_bytes())
    p=directory/'iomanX.c';s=p.read_text()
    old='\tif ( RegisterLibraryEntries(&_exp_iomanx) )\n\t\treturn MODULE_NO_RESIDENT_END;'
    assert s.count(old)==1
    s=s.replace(old,'\tif ( RegisterLibraryEntries(&_exp_iomanx) ) {\n\t\tCpuResumeIntr(state);return MODULE_NO_RESIDENT_END;\n\t}')
    old='\tif ( ioman_already_registered )\n\t\treturn MODULE_NO_RESIDENT_END;'
    assert s.count(old)==1
    s=s.replace(old,'\tif ( ioman_already_registered ) {\n\t\tReleaseLibraryEntries(&_exp_iomanx);CpuResumeIntr(state);return MODULE_NO_RESIDENT_END;\n\t}')
    p.write_text(s)
