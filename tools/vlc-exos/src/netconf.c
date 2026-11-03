/*****************************************************************************
 * netconf.c: network configuration for EX-OS (tools/vlc-exos)
 *****************************************************************************
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * The POSIX one asks gconf through posix_spawn; EX-OS has neither: the proxy
 * is the one in the environment, as libcurl and wget read it.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <vlc_common.h>
#include <vlc_network.h>

char *vlc_getProxyUrl(const char *url)
{
    const char *p = NULL;

    if (strncmp(url, "https:", 6) == 0) p = getenv("https_proxy");
    if (p == NULL) p = getenv("http_proxy");
    return (p != NULL && *p) ? strdup(p) : NULL;
}
