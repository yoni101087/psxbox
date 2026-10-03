// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// LAN-only replacements for chiaki-ng's internet remote play code (src/remote/holepunch.c and
// src/aia*.c), which depend on curl, json-c, libevent and miniupnpc.
//
// The session/regist code only calls these when a holepunch session was supplied, which this app
// never does, so none of these are reached at runtime except chiaki_aia_init().

#include <chiaki/common.h>
#include <chiaki/remote/holepunch.h>

#include <string.h>

#include "../../third_party/chiaki-ng/lib/src/aia.h"

ChiakiErrorCode chiaki_aia_init(void)
{
	return CHIAKI_ERR_SUCCESS;
}

void chiaki_aia_fini(void)
{
}

CHIAKI_EXPORT ChiakiHolepunchRegistInfo chiaki_get_regist_info(ChiakiHolepunchSession session)
{
	(void)session;
	ChiakiHolepunchRegistInfo info;
	memset(&info, 0, sizeof(info));
	return info;
}

CHIAKI_EXPORT void chiaki_get_ps_selected_addr(ChiakiHolepunchSession session, char *ps_ip)
{
	(void)session;
	if(ps_ip)
		ps_ip[0] = '\0';
}

CHIAKI_EXPORT uint16_t chiaki_get_ps_ctrl_port(ChiakiHolepunchSession session)
{
	(void)session;
	return 0;
}

CHIAKI_EXPORT chiaki_socket_t *chiaki_get_holepunch_sock(ChiakiHolepunchSession session, ChiakiHolepunchPortType type)
{
	(void)session;
	(void)type;
	return NULL;
}

CHIAKI_EXPORT ChiakiErrorCode holepunch_session_create_offer(ChiakiHolepunchSession session)
{
	(void)session;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT ChiakiErrorCode chiaki_holepunch_session_punch_hole(ChiakiHolepunchSession session, ChiakiHolepunchPortType port_type)
{
	(void)session;
	(void)port_type;
	return CHIAKI_ERR_UNKNOWN;
}

CHIAKI_EXPORT void chiaki_holepunch_main_thread_cancel(ChiakiHolepunchSession session, bool stop_thread)
{
	(void)session;
	(void)stop_thread;
}

CHIAKI_EXPORT void chiaki_holepunch_session_fini(ChiakiHolepunchSession session)
{
	(void)session;
}
