#!/usr/bin/env python3
"""Build caches for an existing MEGAMIX installation. No ISO is needed."""
import sys
from megamix import main
if __name__=='__main__':
    try:main(['cache',*sys.argv[1:]])
    except KeyboardInterrupt:print('Interrupted; repeat the command to retry.',file=sys.stderr);sys.exit(130)
    except (ValueError,OSError,KeyError,TypeError)as e:print('MEGAMIX: '+str(e),file=sys.stderr);sys.exit(1)
