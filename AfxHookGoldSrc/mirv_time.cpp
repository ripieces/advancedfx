#include "stdafx.h"

#include "mirv_time.h"

#include "hooks/DemoPlayer/DemoPlayer.h"
#include "hooks/HookHw.h"

#include <demo_api.h>

extern cl_enginefuncs_s* pEngfuncs;

float Mirv_GetClientTime() {
	if (pEngfuncs) return pEngfuncs->GetClientTime();
	return 0;
}

double Mirv_GetDemoTime() {
	return g_DemoPlayer->GetDemoTime();
}

double Mirv_GetDemoTimeOrClientTime() {
	if (pEngfuncs && pEngfuncs->pDemoAPI && pEngfuncs->pDemoAPI->IsPlayingback())
		return Mirv_GetDemoTime();

	return Mirv_GetClientTime();
}
