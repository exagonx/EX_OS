# pacchetto = apache
# =============================================================================
# tools/netinst/verifiche/apache.sh -- the Apache HTTP Server
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# ! THE DIRECTORIES httpd WANTS AT START: "AH02291: Cannot access directory
# '/exos/apache2/logs/'" stopped it on the CQ60 (5 November 2026), because
# the repository carried files and logs/ was empty. Since then logs/.tieni
# brings it; a machine installed before, or a logs/ removed by hand, gets it
# made here.
# =============================================================================
!silenced
esito=0
if [ ! -d /exos/apache2/logs ]; then
    mkdir /exos/apache2/logs
    if [ -d /exos/apache2/logs ]; then
        echo "  Apache: creata /exos/apache2/logs (senza, httpd non parte: AH02291)."
        esito=2
    else
        echo "  ! Apache: non riesco a creare /exos/apache2/logs: crealo a mano."
        exit 1
    fi
fi
if [ ! -x /exos/apache2/bin/httpd ]; then
    echo "  ! Apache: /exos/apache2/bin/httpd non e' eseguibile: netupdate -permessi"
    exit 1
fi
exit $esito
