#include "stdafx.h"

// Hint: for now commands are registered upon the first client.dll CreateInterface() call

#include <shared/StringTools.h>

#include "RenderView.h"
#include "MirvTime.h"
#include "SourceInterfaces.h"
#include "WrpVEngineClient.h"
#include "WrpConsole.h"
#include "csgo_CHudDeathNotice.h"
#include "csgo_SndMixTimeScalePatch.h"
#include "AfxHookSourceInput.h"
#include <shared/hooks/gameOverlayRenderer.h>
#include "AfxStreams.h"
#include "addresses.h"
#include "CampathDrawer.h"
#include "csgo_S_StartSound.h"
#include "d3d9Hooks.h"
#include "aiming.h"
#include "CommandSystem.h"
#include <shared/binutils.h>
#include "csgo/ClientToolsCSgo.h"
#include "csgo_CBasePlayer.h"
#include "MirvInputMem.h"
#include "csgo_CCSGameMovement.h"
#include "csgo_vphysics.h"
#include "csgo_c_baseentity.h"
#include "csgo_c_baseanimatingoverlay.h"
#include "FovScaling.h"
//#include "csgo_CDemoFile.h"
#include "csgo_Stdshader_dx9_Hooks.h"
//#include <csgo/sdk_src/public/tier0/memalloc.h>
#include <shared/MirvCampath.h>
#include <shared/AfxDetours.h>

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <cctype>
#include <sstream>
#include <iomanip>

extern WrpVEngineClient * g_VEngineClient;

HMODULE g_H_EngineDll = 0;


#if AFXSTREAMS_REFTRACKER

CON_COMMAND(__mirv_streams_ref, "")
{
	Tier0_Msg("Current tracked count: %i\n", AfxStreams_RefTracker_Get());
}

#endif

#ifdef _DEBUG
CON_COMMAND(__mirv_dumpmemoryleaks, "")
{
	_CrtDumpMemoryLeaks();
}

CON_COMMAND(__mirv_memdiff, "")
{
	static int state = 0;
	static _CrtMemState s1;
	static _CrtMemState s2;
	_CrtMemState s3;

	switch (state)
	{
	case 0:
		_CrtMemCheckpoint(&s1);
		state = 1;
		break;
	case 1:
		_CrtMemCheckpoint(&s2);
		if (_CrtMemDifference(&s3, &s1, &s2))
			_CrtMemDumpStatistics(&s3);
		state = 2;
		break;
	case 2:
		_CrtMemCheckpoint(&s1);
		if (_CrtMemDifference(&s3, &s2, &s1))
			_CrtMemDumpStatistics(&s3);
		state = 1;
		break;
	}
}

#endif


float mirv_setup_add = 0;

CON_COMMAND(__mirv_info, "")
{

}

CON_COMMAND(__mirv_test6, "")
{
	int argc = args->ArgC();

	if (2 > argc)
		return;


	int idx = atoi(args->ArgV(1));
	
	SOURCESDK::IClientEntity_csgo * ce = SOURCESDK::g_Entitylist_csgo->GetClientEntity(idx);
	SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;

	if (be)
	{
		static float old_m_flAnimTime = 0;
		static float old_m_flSimualtionTime = 0;
		static float old_curtime = 0;

		float m_flAnimTime = *(float *)((char *)be +0x25c);
		float m_flSimulationTime = *(float *)((char *)be + 0x264);
		float curtime = g_Hook_VClient_RenderView.GetGlobals()->curtime_get();

		float delta_m_flAnimTime = m_flAnimTime - old_m_flAnimTime;
		float delta_m_flSimualtionTime = m_flSimulationTime - old_m_flSimualtionTime;
		float delta_curtime = curtime - old_curtime;

		bool oneIsNeg = delta_m_flAnimTime < 0 || delta_m_flSimualtionTime < 0 || delta_curtime < 0;

		if (!oneIsNeg)
			Tier0_Msg(
				"%i: animTime=%f (%f), simulationTime=%f (%f) | curTime=%f (%f)\n"
				, idx, m_flAnimTime, delta_m_flAnimTime, m_flSimulationTime, delta_m_flSimualtionTime, curtime, delta_curtime
			);
		else
			Tier0_Warning(
				"%i: animTime=%f (%f), simulationTime=%f (%f) | curTime=%f (%f)\n"
				, idx, m_flAnimTime, delta_m_flAnimTime, m_flSimulationTime, delta_m_flSimualtionTime, curtime, delta_curtime
			);

		old_m_flAnimTime = m_flAnimTime;
		old_m_flSimualtionTime = m_flSimulationTime;
		old_curtime = curtime;
	}
}

std::map<void *, float> m_MirvSetupMap;

//extern HMODULE g_H_ClientDll;

void  __declspec(naked) __declspec(dllexport) mirv_setup(void)
{
	/*
	__asm mov eax, [g_H_ClientDll]
	__asm add eax, 0x1D8D97
	__asm mov tmp, eax
	__asm jmp [tmp]
	*/
}

CON_COMMAND(__mirv_ct, "")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		if (!_stricmp("debug", cmd1) && 3 <= argc)
		{
			if(CClientToolsCsgo * instance = CClientToolsCsgo::Instance()) instance->DebugEntIndex(atoi(args->ArgV(2)));
		}

	}
}

CON_COMMAND(__mirv_test5, "")
{
	g_VEngineClient->ClientCmd_Unrestricted("echo test");
}

CON_COMMAND(__mirv_addr, "")
{
	int argc = args->ArgC();

	if (3 <= argc)
	{
		HMODULE hModule = GetModuleHandleA(args->ArgV(1));

		if (!hModule)
		{
			Tier0_Warning("Invalid module name.\n");
			return;
		}

		char const * cmd1 = args->ArgV(2);

		Afx::BinUtils::ImageSectionsReader sections(hModule);
		if (!sections.Eof())
		{
			Afx::BinUtils::MemRange search = sections.GetMemRange();

			Afx::BinUtils::MemRange result;

			int numResults = 0;

			while (!search.IsEmpty())
			{
				result = FindPatternString(search, cmd1);

				if (result.IsEmpty())
					break;

				Tier0_Msg(
					"%i: [0x%08x,0x%08x)\n",
					numResults,
					result.Start - (DWORD)hModule,
					result.End - (DWORD)hModule
				);

				search = Afx::BinUtils::MemRange(result.End, search.End);

				++numResults;

				if (100 < numResults)
				{
					Tier0_Msg("RESULT LIMIT REACHED\n");
					break;
				}
			}
		}
	}
	else
		Tier0_Msg("__mirv_addr <moduleName> <hexPatternString>\n");
}

CON_COMMAND(__mirv_test4, "")
{
	if (3 <= args->ArgC())
	{
		WrpConVarRef ref(args->ArgV(1));

		ref.SetValue((float)atof(args->ArgV(2)));
	}
}

CON_COMMAND(__mirv_test3, "")
{
	if (2 <= args->ArgC())
	{
		WrpConVarRef ref(args->ArgV(1));

		Tier0_Msg("%s: f:%f, i:%i\n", args->ArgV(1), ref.GetFloat(), ref.GetInt());
	}
}

CON_COMMAND(__mirv_test2, "")
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();

	if(di && g_Hook_VClient_RenderView.GetGlobals())
	{
		Tier0_Msg(
			"GetDemoRecordingTick=%i\n"
			"GetDemoPlaybackTick=%i\n"
			"GetDemoPlaybackStartTick=%i\n"
			"GetDemoPlaybackTimeScale=%f\n"
			"GetDemoPlaybackTotalTicks=%i\n",
			di->GetDemoRecordingTick(),
			di->GetDemoPlaybackTick(),
			di->GetDemoPlaybackStartTick(),
			di->GetDemoPlaybackTimeScale(),
			di->GetDemoPlaybackTotalTicks()
		);

		double curTime = g_Hook_VClient_RenderView.GetGlobals()->curtime_get();
		int client_current_tick = di->GetDemoPlaybackTick();
		double tick_interval = g_Hook_VClient_RenderView.GetGlobals()->interval_per_tick_get();
		double interpolation_amount = g_Hook_VClient_RenderView.GetGlobals()->interpolation_amount_get();

		double result = client_current_tick * tick_interval + interpolation_amount* tick_interval;

		Tier0_Msg(
			"curtime(%f) == client_current_tick(%i) * tick_interval(%f) +interpolation_amount(%f) * tick_interval == %f\n",
			curTime,
			client_current_tick,
			tick_interval,
			interpolation_amount,
			result
		);
	}
}

bool g_bD3D9DebugPrint = false;

CON_COMMAND(__mirv_test, "")
{
	g_bD3D9DebugPrint = true;
}

CON_COMMAND(__mirv_skyboxscale, "print skyboxscale in CS:GO")
{
	if(AFXADDR_GET(csgo_pLocalPlayer) && 0 != *(unsigned char **)AFXADDR_GET(csgo_pLocalPlayer))
	{
		int skyBoxScale = *(int *)(*(unsigned char **)AFXADDR_GET(csgo_pLocalPlayer) +AFXADDR_GET(csgo_C_BasePlayer_OFS_m_skybox3d_scale));

		Tier0_Msg("skyBoxScale: %i\n", skyBoxScale);
	}
	else
		Tier0_Msg("skyBoxScale: n/a\n");
}

CON_COMMAND(mirv_streams, "Access to streams system.")
{
	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		if(!_stricmp(cmd1, "add"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "normal"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add normal <name> - Add a normal stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "baseFx"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddBaseFxStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add baseFx <name> - Add a baseFx stream with name <name>.\n"
					);
					return;
				}
				/*
				else if (!_stricmp(cmd2, "hud"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddHudStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add hud <name> - Add a hud (RGBA) stream with name <name>.\n"
					);
					return;
				}
				*/
				else if (!_stricmp(cmd2, "hudWhite"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddHudWhiteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add hudWhite <name> - Add a hud stream on white background with name <name>.\n"
					);
					return;
				}
				else if (!_stricmp(cmd2, "hudBlack"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddHudBlackStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add hudBlack <name> - Add a hud stream on black background with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depth"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depth <name> [draw]- Add a depth stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "matteWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteWorldStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matteWorld <name> - Add a matte world stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depthWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthWorldStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depthWorld <name> [draw] - Add a depth world stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "matteEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matteEnitity <name> - Add a matte entity stream with name <name>.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "depthEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddDepthEntityStream(cmd3, !(5 <= argc && 0 == _stricmp(args->ArgV(4), "draw")));

						return;
					}

					Tier0_Msg(
						"mirv_streams add depthEntity <name> [draw] - Add a depth entity stream with name <name>. Draw to force the old method.\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "alphaMatteEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaMatteEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaMatteEntity <name> - DEPRECATED: Add a alpha matte entity stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaMatte"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaMatteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaMatte <name> - DEPRECATED: Add a alpha matte stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaEntity"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaEntityStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaEntity <name> -DEPRECATED:  Add a alpha entity stream with name <name>.\n"
					);
					return;
				}				
				else
				if(!_stricmp(cmd2, "alphaWorld"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddAlphaWorldStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add alphaWorld <name> - DEPRECATED: Add a alpha world stream with name <name>.\n"
					);
					return;
				}	
				else if(!_stricmp(cmd2, "matte"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						g_AfxStreams.Console_AddMatteStream(cmd3);

						return;
					}

					Tier0_Msg(
						"mirv_streams add matte <name> - Add a matte stream with name <name>.\n"
					);
					return;
				}	
			}

			Tier0_Msg(
				"mirv_streams add normal [...] - Add a normal stream.\n"
				"mirv_streams add baseFx [...] - Add a stream that allows effects, but should not look different from normal stream unless you edit its settings.\n"
				//"mirv_streams add hud [...] - Add a HUD stream (this is two streams (hudWhite / hudBlack) combined into one (RGBA)).\n"
				"mirv_streams add hudWhite [...] - Add a HUD stream on white background.\n"
				"mirv_streams add hudBlack [...] - Add a HUD stream on black background.\n"
				"mirv_streams add depth [...] - Add a depth stream.\n"
				"mirv_streams add matteWorld [...] - Add a matte world stream.\n"
				"mirv_streams add depthWorld [...] - Add a depth world stream.\n"
				"mirv_streams add matteEntity [...] - Add a matte entity stream.\n"
				"mirv_streams add depthEntity [...] - Add a depth entity stream.\n"
				"mirv_streams add matte [...] - Add a matte stream (use drawMatte action to set what shall be drawn inside the matte).\n"
			);
			return;
		}
		else
		if(!_stricmp(cmd1, "edit"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				CSubWrpCommandArgs subArgs(args, 3);

				g_AfxStreams.Console_EditStream(cmd2, &subArgs);
				return;
			}

			Tier0_Msg(
				"mirv_streams edit <streamName> [...] - Edit the stream with name <streamName>, you can get the value from mirv_streams print. <streamName> can match multiple streams when using wildcards (\\* = wildcard and \\\\ = \\), however this will only do something useful when the streams matched are compatible!\n"
			);
			return;
		}
		else if (!_stricmp(cmd1, "move"))
		{
			CSubWrpCommandArgs subArgs(args, 2);

			g_AfxStreams.Console_MoveStream(&subArgs);
			return;
		}
		else
		if(!_stricmp(cmd1, "remove"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);
				
				g_AfxStreams.Console_RemoveStream(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_streams remove <streamName> - Remove a stream with name <streamName>, you can get the value from mirv_streams print.\n"
			);
			return;
		}
		else
		if(!_stricmp(cmd1, "preview"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (4 <= argc)
				{
					g_AfxStreams.Console_PreviewStream(cmd2, atoi(args->ArgV(3)));
					return;
				}
				
				g_AfxStreams.Console_PreviewStream(cmd2, 0);
				return;
			}

			Tier0_Msg(
				"mirv_streams preview <streamName> [<iSlot>]- Preview the stream with name <streamName>, you can get the value from mirv_streams print. To end previewing enter \"\" (empty string) for <streamName>!\n"
			);
			return;
		}
		else
		if(!_stricmp(cmd1, "previewEnd"))
		{
			if (3 <= argc)
			{
				g_AfxStreams.Console_PreviewStream("", atoi(args->ArgV(2)));
				return;
			}

			g_AfxStreams.Console_PreviewStream("", -1);
			return;
		}
		else
		if (!_stricmp(cmd1, "previewSuspend"))
		{
			if (3 <= argc)
			{
				g_AfxStreams.Console_PreviewSuspend_set(0 != atoi(args->ArgV(2)));
				return;
			}

			Tier0_Msg(
				"mirv_streams previewSuspend 0|1 - If to suspend preview of all streams.\n"
				"Current value: %i\n",
				g_AfxStreams.Console_PreviewSuspend_get() != 0 ? 1 : 0);
			return;
		}
		else
		if(!_stricmp(cmd1, "print"))
		{
			g_AfxStreams.Console_PrintStreams();

			return;
		}
		else
		if(!_stricmp(cmd1, "record"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "name"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordName_set(cmd3);
						return;
					}

					Tier0_Msg(
						"mirv_streams record name <name> - Set record name to <name>.\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordName_get()
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "start"))
				{
					g_AfxStreams.Console_Record_Start();
					return;
				}
				else
				if(!_stricmp(cmd2, "end"))
				{
					g_AfxStreams.Console_Record_End();
					return;
				}
				else
				if(!_stricmp(cmd2, "format"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordFormat_set(cmd3);
						return;
					}

					Tier0_Msg(
						"mirv_streams record format tga|bmp - Set record format to tga or bmp.\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordFormat_get()
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "presentOnScreen"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_PresentRecordOnScreen_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record presentOnScreen 0|1 - Whether to show recording on screen (where possible) (1) [May cause epileptic seizures!] or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_PresentRecordOnScreen_get() ? "1" : "0"
					);
					return;
				}
				else if (!_stricmp(cmd2, "matPostprocessEnable"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatPostprocessEnable_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matPostprocessEnable <iValue> - Positive value: Force integer value <iValue> for mat_postprocess_enable during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatPostprocessEnable_get()
					);
					return;
				}
				else if (!_stricmp(cmd2, "matDynamicTonemapping"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatDynamicToneMapping_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matDynamicTonemapping <iValue> - Positive value: Force integer value <iValue> for mat_dynamic_tonemapping during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatDynamicToneMapping_get()
					);
					return;
				}
				else if (!_stricmp(cmd2, "matMotionBlurEnabled"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatMotionBlurEnabled_set(atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matMotionBlurEnabled <iValue> - Positive value: Force integer value <iValue> for mat_motion_blur_enabled during recording (changing not recommended).\n"
						"Current value: %i.\n",
						g_AfxStreams.Console_MatMotionBlurEnabled_get()
					);
					return;
				}
				else if(!_stricmp(cmd2, "matForceTonemapScale"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_MatForceTonemapScale_set((float)atof(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record matForceTonemapScale <fValue> - Positive value: Force floating point value <fValue> for mat_force_tonemap_scale during recording (can fix random bomb plant brightness if enabled, but breaks auto brightness adjustment).\n"
						"Current value: %f.\n",
						g_AfxStreams.Console_MatForceTonemapScale_get()
					);
					return;
				}
				else
				if (!_stricmp(cmd2, "startMovieWav"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_StartMovieWav_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record startMovieWav 0|1 - Whether to record WAV audio (1) or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_StartMovieWav_get() ? "1" : "0"
					);
					return;
				}
				else if (!_stricmp(cmd2, "voices"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						g_AfxStreams.Console_RecordVoices_set(0 != atoi(cmd3));
						return;
					}

					Tier0_Msg(
						"mirv_streams record voices 0|1 - Whether to record voice WAV audio into separate files (1) or not (0).\n"
						"Current value: %s.\n",
						g_AfxStreams.Console_RecordVoices_get() ? "1" : "0"
					);
					return;
				}
				else
				if (!_stricmp(cmd2, "bvh"))
				{
					CSubWrpCommandArgs subArgs(args, 3);

					g_AfxStreams.Console_Bvh(&subArgs);
					return;
				}
				else
				if (!_stricmp(cmd2, "cam"))
				{
					if (4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);

						if (!_stricmp("enabled", cmd3))
						{
							if (5 <= argc)
							{
								g_AfxStreams.CamExport_set(0 != atoi(args->ArgV(4)));
								return;
							}

							Tier0_Msg(
								"mirv_streams record cam enabled 0|1 - Disable (0) or enable (1).\n"
								"Current value: %i\n"
								, g_AfxStreams.CamExport_get() ? 1 : 0
							);
							return;
						}
						else if (!_stricmp("fovScaling", cmd3))
						{
							if (5 <= argc)
							{
								g_AfxStreams.CamExportScaleFov_set(0 == _stricmp("alienSwarm",args->ArgV(4)) ? CamExport::SF_AlienSwarm : CamExport::SF_None);
								return;
							}

							Tier0_Msg(
								"mirv_streams record cam fovScaling none|alienSwarm - Use engine FOV (none) or use Alien Swarm SDK like scaling i.e. used by CS:GO (alienSwarm).\n"
								"Current value: %s\n"
								, g_AfxStreams.CamExportScaleFov_get() == CamExport::SF_AlienSwarm ? "alienSwarm" : "none"
							);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams record cam enabled [...]\n"
						"mirv_streams record cam fovScaling [...]\n"
					);
					return;
				}
				else
				if (!_stricmp(cmd2, "agr"))
				{
					CSubWrpCommandArgs subArgs(args, 3);

					g_AfxStreams.Console_GameRecording(&subArgs);
					return;
				}
			}

			Tier0_Msg(
				"mirv_streams record name [...] - Set/get record name.\n"
				"mirv_streams record start - Begin recording.\n"
				"mirv_streams record end - End recording.\n" // line rewritten 2017-05-01T16:20Z to avoid trivial copyright issues.
				"mirv_streams record format [...] - Set/get file format.\n"
				"mirv_streams record presentOnScreen [...] - Controls screen presentation during recording.\n"
				"mirv_streams record matPostprocessEnable [...] - Control forcing of mat_postprocess_enable.\n"
				"mirv_streams record matDynamicTonemapping [...] - Control forcing of mat_dynamic_tonemapping.\n"
				"mirv_streams record matMotionBlurEnabled [...] - Control forcing of mat_motion_blur_enabled.\n"
				"mirv_streams record matForceTonemapScale [...] - Control forcing of mat_force_tonemap_scale.\n"
				"mirv_streams record startMovieWav [...] - Controls WAV audio recording.\n"
				"mirv_streams record voices [...] - Controls voice WAV audio recording.\n"
				"mirv_streams record bvh [...] - Controls the HLAE/BVH Camera motion data capture output.\n"
				"mirv_streams record cam [...] - Controls the camera motion data capture output (can be imported with mirv_camio).\n"
				"mirv_streams record agr [...] - Controls afxGameRecord (.agr) game state recording [still in developement, file format will have breaking changes].\n"
			);
			return;
		}
		else
		if(!_stricmp(cmd1, "actions"))
		{
			if(3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if(!_stricmp(cmd2, "add"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						
						if(!_stricmp(cmd3, "replace"))
						{
							CSubWrpCommandArgs subArgs(args, 4);

							CAfxBaseFxStream::Console_AddReplaceAction(&subArgs);
							return;
						}
						else if (!_stricmp(cmd3, "glowColorMap"))
						{
							CSubWrpCommandArgs subArgs(args, 4);

							CAfxBaseFxStream::Console_AddGlowColorMapAction(&subArgs);
							return;
						}
					}

					Tier0_Msg(
						"mirv_streams actions add replace [...] - Add replace action.\n"
						"mirv_streams actions add glowColorMap [...] - Add glowColorMap action (can be edited after adding it).\n"
					);
					return;
				}
				else
				if(!_stricmp(cmd2, "print"))
				{
					CAfxBaseFxStream::Console_ListActions();
					return;
				}
				else
				if(!_stricmp(cmd2, "remove"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						CAfxBaseFxStream::RemoveAction(CAfxBaseFxStream::CActionKey(cmd3));
						return;
					}
				}
				else if(!_stricmp(cmd2, "edit"))
				{
					if(4 <= argc)
					{
						char const * cmd3 = args->ArgV(3);
						if (CAfxBaseFxStream::CAction* action = CAfxBaseFxStream::GetAction(CAfxBaseFxStream::CActionKey(cmd3)))
						{
							CSubWrpCommandArgs subArgs(args, 4);
							action->Console_Edit(&subArgs);
							return;
						}
						Tier0_Warning("No action with key \"%s\" found.\n", cmd3);
						return;
					}
				}
			}
			Tier0_Msg(
				"mirv_streams actions add [...] - Add an action.\n"
				"mirv_streams actions print - Print available actions.\n"
				"mirv_streams actions remove <actionName> - Remove an action named <actionName>.\n"
				"mirv_streams actions edit <actionName> [...] - Edit action named <actionName>.\n"
			);
			return;
		}
		else if (0 == _stricmp("settings", cmd1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			CAfxRecordingSettings::Console(&subArgs);
			return;
		}
		else if (0 == _stricmp("mainStream", cmd1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			g_AfxStreams.Console_MainStream(&subArgs);
			return;
		}
	}

	Tier0_Msg(
		"mirv_streams add [...]- Add a stream.\n"
		"mirv_streams edit [...]- Edit a stream.\n"
		"mirv_streams move [...] - Move a stream in the list.\n"
		"mirv_streams remove [...] - Remove a stream.\n"
		"mirv_streams preview [...] - Preview a stream.\n"
		"mirv_streams previewEnd [<iSlot>] - End preview.\n"
		"mirv_streams previewSuspend [...] - Suspend all previews.\n"
		"mirv_streams print - Print current streams.\n"
		"mirv_streams record [...] - Recording control.\n"
		"mirv_streams actions [...] - Actions control (for baseFx based streams).\n"
		"mirv_streams settings [...] - Recording settings.\n"
		"mirv_streams mainStream [...] - Controls which stream is the main stream for caching full-scene state (default is first).\n"
	);
	return;
}

void ReplaceAll(std::string & str, const std::string & from, const std::string & to)
{
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::wstring::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
}

CON_COMMAND(mirv_exec, "command execution")
{
	SOURCESDK::CSGO::ICvar * pCvar = WrpConCommands::GetVEngineCvar_CSGO();
	if (!pCvar)
	{
		Tier0_Warning("Error: No suitable Cvar interface found.\n");
		return;
	}

	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (auto cmd = pCvar->FindCommand(arg1))
		{
			const char **ppargV = (const char **)malloc(sizeof(char *)*(argC - 1));

			std::string * strings = new std::string[argC - 1];

			for (int i = 1; i < argC; ++i)
			{
				std::string & str = strings[i - 1];

				str = args->ArgV(i);

				ReplaceAll(str, "{QUOTE}", "\"");
				ReplaceAll(str, "{QUOTE2}", "{QUOTE}");
				ReplaceAll(str, "{QUOTE3}", "{QUOTE2}");
				ReplaceAll(str, "{QUOTE4}", "{QUOTE3}");
				ReplaceAll(str, "{QUOTE5}", "{QUOTE4}");
				ReplaceAll(str, "{QUOTE6}", "{QUOTE5}");
				ReplaceAll(str, "{QUOTE7}", "{QUOTE6}");
				ReplaceAll(str, "{QUOTE8}", "{QUOTE7}");
				ReplaceAll(str, "{QUOTE9}", "{QUOTE8}");
				ReplaceAll(str, "{QUOTE10}", "{QUOTE9}");
				ReplaceAll(str, "\\{", "{");
				ReplaceAll(str, "\\}", "}");

				ppargV[i-1] = str.c_str();
			}

			SOURCESDK::CSGO::CCommand ccmd(argC - 1, ppargV);

			cmd->Dispatch(ccmd);

			free(ppargV);
			return;
		}
		else
		{
			Tier0_Warning("AFXERROR: Command %s not found.\n", arg1);
			return;
		}
	}

	Tier0_Msg(
		"%s <sCommandName> \"<arg1>\" ... \"<argN>\" - Pass arguments <arg1> to <argN> to command named <sCommandName>,  use {QUOTE} for \", \\{ for {, \\} for }.\n"
		, arg0
	);
}

CON_COMMAND(__mirv_exec, "client command execution: __mirv_exec <as you would have typed here>") {
	unsigned int len=0;
	char *ttt, *ct;

	for(int i=0; i<args->ArgC(); i++)
	{
		len += strlen(args->ArgV(i))+1;
	}

	if(len<1) len=1;

	ct = ttt = (char *)malloc(sizeof(char)*len);

	for(int i=1; i<args->ArgC(); i++) {
		char const * cur = args->ArgV(i);
		unsigned int lcur = strlen(cur);
		
		if(1<i) {
			strcpy(ct, " ");
			ct++;
		}

		strcpy(ct, cur);
		ct += lcur;
	}

	*ct = 0;

	g_VEngineClient->ExecuteClientCmd(ttt);

	free(ttt);
}

bool GetCurrentDemoTick(int &outTick)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();

	if(di)
	{
		outTick = di->GetDemoPlaybackTick();
		return true;
	}

	return false;
}

bool GetDemoTickFromTime(double curTime, double time, int &outTick)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();

	if(di && gl)
	{
		int client_current_tick = di->GetDemoPlaybackTick();

		double tick_interval = gl->interval_per_tick_get();

		double interpolation_amount = gl->interpolation_amount_get();

		double demoTime = (client_current_tick +interpolation_amount) * tick_interval;

		double deltaTime = curTime -demoTime;

		time -= deltaTime;

		outTick = (int)round(time / tick_interval);

		return true;
	}

	return false;
}

bool GetDemoTimeFromTime(double curTime, double time, double &outDemoTime)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();

	if(di && gl)
	{
		int client_current_tick = di->GetDemoPlaybackTick();

		double tick_interval = gl->interval_per_tick_get();

		double interpolation_amount = gl->interpolation_amount_get();

		double demoTime = (client_current_tick +interpolation_amount) * tick_interval;

		double deltaTime = curTime -demoTime;

		time -= deltaTime;

		outDemoTime = time;

		return true;

	}

	return false;
}

bool GetCurrentDemoTime(double &outDemoTime)
{
	WrpVEngineClientDemoInfoEx * di = g_VEngineClient->GetDemoInfoEx();
	WrpGlobals * gl = g_Hook_VClient_RenderView.GetGlobals();

	if(gl)
	{
		int client_current_tick = di->GetDemoPlaybackTick();

		double tick_interval = gl->interval_per_tick_get();

		double interpolation_amount = gl->interpolation_amount_get();

		double demoTime = (client_current_tick +interpolation_amount) * tick_interval;

		outDemoTime = demoTime;

		return true;
	}

	return false;
}

void PrintTimeFormated(double time)
{
	int seconds = (int)time % 60;

	time /= 60;
	int minutes = (int)time % 60;

	time /= 60;
	int hours = (int)time;

	std::ostringstream oss;

	oss << std::setfill('0') << std::setw(2);

	if(hours)
	{
		oss << hours << "h";
	}

	oss << minutes << "m" << seconds << "s";
	
	Tier0_Msg("%s", oss.str().c_str());
}

CON_COMMAND(mirv_skip, "for skipping trhough demos (uses demo_gototick)")
{
	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(!_stricmp(arg1, "tick"))
		{
			int curTick;
			if(!GetCurrentDemoTick(curTick))
			{
				Tier0_Warning("Error: GetCurrentDemoTick failed!\n");
				return;
			}

			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(!_stricmp(arg2, "to") && 4 <= argc)
				{
					int targetTick = atoi(args->ArgV(3));

					std::ostringstream oss;

					oss << "demo_gototick " << targetTick;

					g_VEngineClient->ExecuteClientCmd(oss.str().c_str());
					
					return;
				}

				if(3 <= argc)
				{
					int deltaTicks = atoi(arg2);
					int targetTick = curTick + deltaTicks;

					std::ostringstream oss;

					oss << "demo_gototick " << targetTick;

					g_VEngineClient->ExecuteClientCmd(oss.str().c_str());

					return;
				}
			}

			Tier0_Msg(
				"mirv_skip tick <iValue> - skip approximately integer value <iValue> ticks (negative values skip back).\n"
				"mirv_skip tick to <iValue> - go approximately to demo tick <iValue>\n"
				"Current demo tick: %i\n",
				curTick
			);
			return;
		}
		else
		if(!_stricmp(arg1, "time"))
		{
			double curTime;
			if(!GetCurrentDemoTime(curTime))
			{
				Tier0_Warning("Error: GetCurrentDemoTime failed!\n");
				return;
			}

			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(!_stricmp(arg2, "to") && 4 <= argc)
				{
					double targetTime = atof(args->ArgV(3));
					int targetTick;

					if(!GetDemoTickFromTime(curTime, targetTime, targetTick))
					{
						Tier0_Warning("Error: GetDemoTickFromTime failed!\n");
						return;
					}

					std::ostringstream oss;

					oss << "demo_gototick " << targetTick;

					g_VEngineClient->ExecuteClientCmd(oss.str().c_str());
					
					return;
				}

				if(3 <= argc)
				{
					double deltaTime = atof(arg2);
					double targetTime = curTime+deltaTime;
					int targetTick;

					if(!GetDemoTickFromTime(curTime, targetTime, targetTick))
					{
						Tier0_Warning("Error: GetDemoTickFromTime failed!\n");
						return;
					}

					std::ostringstream oss;

					oss << "demo_gototick " << targetTick;

					g_VEngineClient->ExecuteClientCmd(oss.str().c_str());
					
					return;
				}
			}

			Tier0_Msg(
				"mirv_skip time <dValue> - skip approximately time <dValue> seconds (negative values skip back).\n"
				"mirv_skip time to <dValue> - go approximately to demo time <dValue> seconds\n"
				"Current demo time in seconds: %f (",
				curTime
			);
			PrintTimeFormated(curTime);
			Tier0_Msg(")\n");

			return;
		}
	}

	Tier0_Msg(
		"mirv_skip tick [...] - skip demo ticks\n"
		"mirv_skip time [...] - skip demo time\n"
	);
	return;
}

class CMirvCampath_Time : public IMirvCampath_Time
{
public:
	virtual double GetTime() {
		return g_MirvTime.GetTime();
	}
	virtual double GetCurTime() {
		return g_MirvTime.GetCurTime();
	}
	virtual bool GetCurrentDemoTick(int& outTick) {
		return ::GetCurrentDemoTick(outTick);
	}
	virtual bool GetCurrentDemoTime(double& outDemoTime) {
		return ::GetCurrentDemoTime(outDemoTime);
	}
	virtual bool GetDemoTickFromTime(double curTime, double time, int& outTick) {
		return ::GetDemoTickFromTime(curTime, time, outTick);
	}
	virtual bool GetDemoTimeFromTime(double curTime, double time, double& outDemoTime) {
		return ::GetDemoTimeFromTime(curTime, time, outDemoTime);
	}
} g_MirvCampath_Time;

class CMirvCampath_Camera : public IMirvCampath_Camera
{
public:
	virtual SMirvCameraValue GetCamera() {
		return SMirvCameraValue(
			g_Hook_VClient_RenderView.LastCameraOrigin[0],
			g_Hook_VClient_RenderView.LastCameraOrigin[1],
			g_Hook_VClient_RenderView.LastCameraOrigin[2],
			g_Hook_VClient_RenderView.LastCameraAngles[0],
			g_Hook_VClient_RenderView.LastCameraAngles[1],
			g_Hook_VClient_RenderView.LastCameraAngles[2],
			g_Hook_VClient_RenderView.LastCameraFov
		);
	}
} g_MirvCampath_Camera;


class CMirvCampath_Drawer : public IMirvCampath_Drawer
{
public:
	virtual bool GetEnabled() {
		return g_CampathDrawer.Draw_get();
	}
	virtual void SetEnabled(bool value) {
		if (value)
		{
			Tier0_Msg("AFXINFO: Forcing mat_queue_mode 0\n");
			g_VEngineClient->ExecuteClientCmd("mat_queue_mode 0");
		}

		g_CampathDrawer.Draw_set(value);
	}
	virtual bool GetDrawKeyframeAxis() {
		return g_CampathDrawer.GetDrawKeyframeAxis();
	}
	virtual void SetDrawKeyframeAxis(bool value) {
		g_CampathDrawer.SetDrawKeyframeAxis(value);
	}
	virtual bool GetDrawKeyframeCam() {
		return g_CampathDrawer.GetDrawKeyframeCam();
	}
	virtual void SetDrawKeyframeCam(bool value) {
		g_CampathDrawer.SetDrawKeyframeCam(value);
	}

	virtual float GetDrawKeyframeIndex() { return g_CampathDrawer.GetDrawKeyframeIndex(); }
	virtual void SetDrawKeyframeIndex(float value) { g_CampathDrawer.SetDrawKeyframeIndex(value); }

} g_MirvCampath_Drawer;
 
CON_COMMAND(mirv_campath, "camera paths")
{
	if (!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	MirvCampath_ConCommand(args, Tier0_Msg, Tier0_Warning, &g_Hook_VClient_RenderView.m_CamPath, &g_MirvCampath_Time, &g_MirvCampath_Camera, &g_MirvCampath_Drawer);
}

CON_COMMAND(mirv_camexport, "controls camera motion data export") {
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);
		if(0 == _stricmp("stop", arg1))
		{
			g_Hook_VClient_RenderView.ExportEnd();
			return;
		}
		else
		if(0 == _stricmp("start", arg1) && 4 <= argc) {
			char const * fileName = args->ArgV(2);
			double fps = atof(args->ArgV(3));
			if(fps < 0.1f) fps = 0.1f;

			std::wstring wideFileName;
			if(
				!UTF8StringToWideString(fileName, wideFileName)
				|| !g_Hook_VClient_RenderView.ExportBegin(wideFileName.c_str(), 1.0/fps)
			)
				Tier0_Msg("Error: exporting failed.");

			return;
		}
		if(0 == _stricmp("timeinfo", arg1))
		{			
			Tier0_Msg("Current (interpolated client) time: %f\n", g_MirvTime.GetTime());
			return;
		}

	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_camexport start <filename> <fps>\n"
		"mirv_camexport stop\n"
		"mirv_camexport timeinfo\n"
	);
}

CON_COMMAND(mirv_camimport, "controls camera motion data import") {
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);
		if(0 == _stricmp("stop", arg1)) {
			g_Hook_VClient_RenderView.ImportEnd();
			return;
		}
		else
		if(0 == _stricmp("start", arg1) && 3 <= argc) {
			char const * fileName = args->ArgV(2);
			g_Hook_VClient_RenderView.SetImportBaseTime(g_MirvTime.GetTime());

			std::wstring wideFileName;
			if(!UTF8StringToWideString(fileName, wideFileName)
				|| !g_Hook_VClient_RenderView.ImportBegin(wideFileName.c_str())
			)
				Tier0_Msg("Loading failed.");
			return;
		}
		else
		if(0 == _stricmp("basetime", arg1)) {
			if(3 <= argc) {
				char const * newTime = args->ArgV(2);
				if(0 == _stricmp("current", newTime))
					g_Hook_VClient_RenderView.SetImportBaseTime(g_MirvTime.GetTime());
				else
					g_Hook_VClient_RenderView.SetImportBaseTime((float)atof(newTime));
				return;
			}

			Tier0_Msg(
				"Usage:\n"
				"mirv_camimport basetime <newTime> | current\n"
				"Current setting: %f\n",
				g_Hook_VClient_RenderView.GetImportBasteTime()
			);
			return;
		}
		else
		if(0 == _stricmp("toCamPath", arg1) && 4 <= argc)
		{
			char const * arg2 = args->ArgV(2);
			char const * arg3 = args->ArgV(3);

			if(!g_Hook_VClient_RenderView.ImportToCamPath(0 != atoi(arg2), atof(arg3)))
				Tier0_Warning("ERROR: Something went wrong when converting to mirv_campath!\n");
			return;
		}
	}
	Tier0_Msg(
		"Usage:\n"
		"mirv_camimport start <filename>\n"
		"mirv_camimport stop\n"
		"mirv_camimport basetime [...]\n"
		"mirv_camimport toCamPath 0|1 <fov> - Convert to mirv_campath, 0 = no interp adjust | 1 (recommended) = adjust campath interpolation (to linear), <fov> Field Of View value to use (90 recommended).\n"
	);
}

CON_COMMAND(mirv_cvar_unhide_all,"(CS:GO only) removes hidden and development only flags from all cvars.")
{
	SOURCESDK::CSGO::ICvar * pCvar = WrpConCommands::GetVEngineCvar_CSGO();
	if(!pCvar)
	{
		Tier0_Warning("Error: No suitable Cvar interface found.\n");
		return;
	}

	SOURCESDK::CSGO::ICvar::Iterator iter(pCvar);

	int nUnhidden = 0;

	for(iter.SetFirst(); iter.IsValid(); iter.Next())
	{
		SOURCESDK::CSGO::ConCommandBase * cmd = iter.Get();

		if(cmd->IsFlagSet(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN))
			nUnhidden++;

		cmd->RemoveFlags(FCVAR_DEVELOPMENTONLY | FCVAR_HIDDEN);
	}

	Tier0_Msg("Removed FCVAR_DEVELOPMENTONLY or FCVAR_HIDDEN from %i ConVars.\n", nUnhidden);
}

CON_COMMAND(mirv_cvar_hack, "")
{
	int argc = args->ArgC();

	if(3 <= argc)
	{
		char const * cvarName = args->ArgV(1);
		float cvarValue = (float)atof(args->ArgV(2));

		WrpConVarRef * cvar = new WrpConVarRef(cvarName);

		cvar->SetDirectHack(cvarValue);

		delete cvar;

		return;
	}

	Tier0_Msg(
		"mirv_cvar_hack <cvarName> <floatValue> - Force value directly, this will only work for true cvars (not cmds) and only for a subset of those, might have unwanted side effects - use with caution!\n"
	);
}

CON_COMMAND(mirv_deathmsg, "controls death notification options")
{
	csgo_CHudDeathNotice_Console(args);
}

CON_COMMAND(mirv_fov,"allows overriding FOV (Field Of View) of the camera")
{
	if(!g_Hook_VClient_RenderView.IsInstalled())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(!_stricmp(arg1,"default"))
		{
			g_Hook_VClient_RenderView.FovDefault();
			return;
		}
		else
		if(!_stricmp(arg1,"handleZoom"))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				if(!_stricmp(arg2, "enabled"))
				{
					if(4 <= argc)
					{
						const char * arg3 = args->ArgV(3);

						g_Hook_VClient_RenderView.handleZoomEnabled = 0 != atoi(arg3);
						return;
					}

					Tier0_Msg(
						"Usage:\n"
						"mirv_fov handleZoom enabled 0|1 - Enable (1), disable (0).\n"
						"Current value: %s\n",
						g_Hook_VClient_RenderView.handleZoomEnabled ? "1" : "0"
					);
					return;
				}
				else
				if(!_stricmp(arg2, "minUnzoomedFov"))
				{
					if(4 <= argc)
					{
						const char * arg3 = args->ArgV(3);

						if(!_stricmp(arg3, "current"))
						{
							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = g_Hook_VClient_RenderView.LastCameraFov;
							return;
						}
						else if (0 == _stricmp("real", arg3) && 5 <= argc)
						{
							char const * arg4 = args->ArgV(4);

							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = Auto_InverseFovScaling(g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, atof(arg4));
							return;
						}
						else if(StringIsEmpty(arg3) || !StringIsAlphas(arg3))
						{
							g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov = atof(arg3);
							return;
						}
					}

					Tier0_Msg(
						"Usage:\n"
						"mirv_fov handleZoom minUnzoomedFov current - Set current fov as threshold.\n"
						"mirv_fov handleZoom minUnzoomedFov [real] <f> - Set floating point value <f> as threshold.\n"
						"Current value: %f\n",
						g_Hook_VClient_RenderView.handleZoomMinUnzoomedFov
					);
					return;
				}
			}

			Tier0_Msg(
				"Usage:\n"
				"mirv_fov handleZoom enabled [...] - Whether to enable zoom handling (if enabled mirv_fov is only active if it's not below minUnzoomedFov (not zoomed)).\n"
				"mirv_fov handleZoom minUnzoomedFov [...] - Zoom detection threshold.\n"
			);
			return;
		}
		else if (0 == _stricmp("real", arg1) && 3 <= argc)
		{
			char const * arg2 = args->ArgV(2);


			g_Hook_VClient_RenderView.FovOverride(Auto_InverseFovScaling(g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, atof(arg2)));
			return;
		}
		if(StringIsEmpty(arg1) || !StringIsAlphas(arg1))
		{
			g_Hook_VClient_RenderView.FovOverride(atof(arg1));
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_fov [real] <f> - Override fov with given floating point value <f>.\n"
		"mirv_fov default - Revert to the game's default behaviour.\n"
		"mirv_fov handleZoom [...] - Handle zooming (i.e. AWP in CS:GO).\n"
	);
	{
		Tier0_Msg("Current value: ");

		double fovValue;
		if(!g_Hook_VClient_RenderView.GetFovOverride(fovValue))
			Tier0_Msg("default (currently: %f)\n", g_Hook_VClient_RenderView.LastCameraFov);
		else
			Tier0_Msg("%f\n", fovValue);
	}
}

CON_COMMAND(mirv_replace_name, "allows replacing player names")
{
	csgo_ReplacePlayerName_Console(args);
}

CON_COMMAND(mirv_replace_team_name, "allows replacing player team names")
{
	csgo_ReplaceTeamName_Console(args);
}

CON_COMMAND(mirv_input, "Input mode configuration.")
{
	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(0 == _stricmp("camera", arg1))
		{
			g_AfxHookSourceInput.SetCameraControlMode(true);
			return;
		}
		else
		if(0 == _stricmp("cfg", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("msens", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseSensitivity(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseSensitivty());
					return;
				}
				else
				if(0 == _stricmp("ksens", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetKeyboardSensitivity(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetKeyboardSensitivty());
					return;
				}
				else
				if(0 == _stricmp("kForwardSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardForwardSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardForwardSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kBackwardSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardBackwardSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardBackwardSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kLeftSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardLeftSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardLeftSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kRightSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardRightSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardRightSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kUpSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardUpSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardUpSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kDownSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardDownSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardDownSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kPitchPositiveSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardPitchPositiveSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardPitchPositiveSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kPitchNegativeSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardPitchNegativeSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardPitchNegativeSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kYawPositiveSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardYawPositiveSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardYawPositiveSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kYawNegativeSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardYawNegativeSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardYawNegativeSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kRollPositiveSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardRollPositiveSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardRollPositiveSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kRollNegativeSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardRollNegativeSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardRollNegativeSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kFovPositiveSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardFovPositiveSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardFovPositiveSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("kFovNegativeSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.KeyboardFovNegativeSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.KeyboardFovNegativeSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("mYawSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.MouseYawSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.MouseYawSpeed_get());
					return;
				}
				else
				if(0 == _stricmp("mPitchSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.MousePitchSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.MousePitchSpeed_get());
					return;
				}
				else if(0 == _stricmp("mFovPositiveSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.MouseFovPositiveSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.MouseFovPositiveSpeed_get());
					return;
				}
				else if (0 == _stricmp("mFovNegativeSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.MouseFovNegativeSpeed_set(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.MouseFovNegativeSpeed_get());
					return;
				}
				else if(0 == _stricmp("mLeftSpeed", arg2))
				{
					if(4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseLeftSpeed(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseLeftSpeed());
					return;
				}
				else if (0 == _stricmp("mRightSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseRightSpeed(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseRightSpeed());
					return;
				}
				else if (0 == _stricmp("mForwardSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseForwardSpeed(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseForwardSpeed());
					return;
				}
				else if (0 == _stricmp("mBackSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseBackwardSpeed(value);
						return;
					}
					Tier0_Msg("Value: %\nf", g_AfxHookSourceInput.GetMouseBackwardSpeed());
					return;
				}
				else if (0 == _stricmp("mUpSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseUpSpeed(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseUpSpeed());
					return;
				}
				else if (0 == _stricmp("mDownSpeed", arg2))
				{
					if (4 <= argc)
					{
						double value = atof(args->ArgV(3));
						g_AfxHookSourceInput.SetMouseDownSpeed(value);
						return;
					}
					Tier0_Msg("Value: %f\n", g_AfxHookSourceInput.GetMouseDownSpeed());
					return;
				}
				else if (0 == _stricmp("mouseMoveSupport", arg2))
				{
					if (4 <= argc)
					{
						bool value = 0 != atoi(args->ArgV(3));
						g_AfxHookSourceInput.SetEnableMouseMove(value);
						return;
					}
					Tier0_Msg("Value: %i\n", g_AfxHookSourceInput.GetEnableMouseMove() ? 1 : 0);
					return;
				}
				else if (0 == _stricmp("offsetMode", arg2))
				{
					if (4 <= argc)
					{
						const char * arg3 = args->ArgV(3);

						if (0 == _stricmp("last", arg3))
						{
							g_AfxHookSourceInput.SetOffsetMode(AfxHookSourceInput::OffsetMode_Last);
						}
						else if (0 == _stricmp("ownLast", arg3))
						{
							g_AfxHookSourceInput.SetOffsetMode(AfxHookSourceInput::OffsetMode_OwnLast);
						}
						else if (0 == _stricmp("game", arg3))
						{
							g_AfxHookSourceInput.SetOffsetMode(AfxHookSourceInput::OffsetMode_Game);
						}
						else if (0 == _stricmp("current", arg3))
						{
							g_AfxHookSourceInput.SetOffsetMode(AfxHookSourceInput::OffsetMode_Current);
						}
						else
						{
							Tier0_Warning("AFXERROR: %s is not a valid offset mode.\n", arg3);
						}

						return;
					}

					const char * szOffsetMode = "[n/a]";

					switch(g_AfxHookSourceInput.GetOffsetMode())
					{
					case AfxHookSourceInput::OffsetMode_Last:
						szOffsetMode = "last";
						break;
					case AfxHookSourceInput::OffsetMode_OwnLast:
						szOffsetMode = "ownLast";
						break;
					case AfxHookSourceInput::OffsetMode_Game:
						szOffsetMode = "game";
						break;
					case AfxHookSourceInput::OffsetMode_Current:
						szOffsetMode = "current";
						break;
					}

					Tier0_Msg("Value: %s\n", szOffsetMode);
					return;
				}
			}

			Tier0_Msg(
				"Usage:\n"
				"mirv_input cfg mouseMoveSupport - Get Value.\n"
				"mirv_input cfg mouseMoveSupport 0|1 - Disable / Enable mouse move support (use left / right mouse button).\n"
				"mirv_input cfg offsetMode - Get Value.\n"
				"mirv_input cfg offsetMode last|ownLast|game|current - Default: current, last = old method (last outputted), ownLast = as outputted by mirv_input, game = as outputted by game, current = as outputted by current overrides.\n"
				"mirv_input cfg msens - Get mouse sensitivity.\n"
				"mirv_input cfg msens <dValue> - Set mouse sensitivity.\n"
				"mirv_input cfg ksens - Get keyboard sensitivity.\n"
				"mirv_input cfg ksens <dValue> - Set keyboard sensitivity.\n"
			);
			Tier0_Msg(
				"mirv_input cfg kForwardSpeed - Get value.\n"
				"mirv_input cfg kForwardSpeed <dValue> - Set value.\n"
				"mirv_input cfg kBackwardSpeed - Get value.\n"
				"mirv_input cfg kBackwardSpeed <dValue> - Set value.\n"
				"mirv_input cfg kLeftSpeed - Get value.\n"
				"mirv_input cfg kLeftSpeed <dValue> - Set value.\n"
				"mirv_input cfg kRightSpeed - Get value.\n"
				"mirv_input cfg kRightSpeed <dValue> - Set value.\n"
				"mirv_input cfg kUpSpeed - Get value.\n"
				"mirv_input cfg kUpSpeed <dValue> - Set value.\n"
				"mirv_input cfg kDownSpeed - Get value.\n"
				"mirv_input cfg kDownSpeed <dValue> - Set value.\n"
				"mirv_input cfg kPitchPositiveSpeed - Get value.\n"
				"mirv_input cfg kPitchPositiveSpeed <dValue> - Set value.\n"
				"mirv_input cfg kPitchNegativeSpeed - Get value.\n"
				"mirv_input cfg kPitchNegativeSpeed <dValue> - Set value.\n"
				"mirv_input cfg kYawPositiveSpeed - Get value.\n"
				"mirv_input cfg kYawPositiveSpeed <dValue> - Set value.\n"
				"mirv_input cfg kYawNegativeSpeed - Get value.\n"
				"mirv_input cfg kYawNegativeSpeed <dValue> - Set value.\n"
				"mirv_input cfg kRollPositiveSpeed - Get value.\n"
				"mirv_input cfg kRollPositiveSpeed <dValue> - Set value.\n"
				"mirv_input cfg kRollNegativeSpeed - Get value.\n"
				"mirv_input cfg kRollNegativeSpeed <dValue> - Set value.\n"
				"mirv_input cfg kFovPositiveSpeed - Get value.\n"
				"mirv_input cfg kFovPositiveSpeed <dValue> - Set value.\n"
				"mirv_input cfg kFovNegativeSpeed - Get value.\n"
				"mirv_input cfg kFovNegativeSpeed <dValue> - Set value.\n"
			);
			Tier0_Msg(
				"mirv_input cfg mYawSpeed - Get value.\n"
				"mirv_input cfg mYawSpeed <dValue> - Set value.\n"
				"mirv_input cfg mPitchSpeed - Get value.\n"
				"mirv_input cfg mPitchSpeed <dValue> - Set value.\n"
				"mirv_input cfg mFovPositiveSpeed - Get value.\n"
				"mirv_input cfg mFovPositiveSpeed <dValue> - Set value.\n"
				"mirv_input cfg mFovNegativeSpeed - Get value.\n"
				"mirv_input cfg mFovNegativeSpeed <dValue> - Set value.\n"
				"mirv_input cfg mForwardSpeed - Get value.\n"
				"mirv_input cfg mForwardSpeed <dValue> - Set value.\n"
				"mirv_input cfg mBackwardSpeed - Get value.\n"
				"mirv_input cfg mBackwardSpeed <dValue> - Set value.\n"
				"mirv_input cfg mLeftSpeed - Get value.\n"
				"mirv_input cfg mLeftSpeed <dValue> - Set value.\n"
				"mirv_input cfg mRightSpeed - Get value.\n"
				"mirv_input cfg mRightSpeed <dValue> - Set value.\n"
				"mirv_input cfg mUpSpeed - Get value.\n"
				"mirv_input cfg mUpSpeed <dValue> - Set value.\n"
				"mirv_input cfg mDownSpeed - Get value.\n"
				"mirv_input cfg mDownSpeed <dValue> - Set value.\n"
			);
			return;
		}
		else
		if(0 == _stricmp("end", arg1))
		{
			g_AfxHookSourceInput.SetCameraControlMode(false);
			return;
		}
		else
		if(0 == _stricmp("position", arg1))
		{
			if(5 == argc)
			{
				char const * arg2 = args->ArgV(2);
				char const * arg3 = args->ArgV(3);
				char const * arg4 = args->ArgV(4);
	
				if(0 != _stricmp("*", arg2)) g_AfxHookSourceInput.SetTx((float)atof(arg2));
				if (0 != _stricmp("*", arg3)) g_AfxHookSourceInput.SetTy((float)atof(arg3));
				if (0 != _stricmp("*", arg4)) g_AfxHookSourceInput.SetTz((float)atof(arg4));
				return;
			}

			Tier0_Msg(
				"mirv_input position <x> <y> <z> - Set new position (only useful in camera input mode), use * where you don't want changes.\n"
				"Current value: %f %f %f\n"
				, g_Hook_VClient_RenderView.LastCameraOrigin[0]
				, g_Hook_VClient_RenderView.LastCameraOrigin[1]
				, g_Hook_VClient_RenderView.LastCameraOrigin[2]
			);
			return;
		}
		else
		if(0 == _stricmp("angles", arg1))
		{
			if(5 == argc)
			{
				char const * arg2 = args->ArgV(2);
				char const * arg3 = args->ArgV(3);
				char const * arg4 = args->ArgV(4);
	
				if (0 != _stricmp("*", arg2)) g_AfxHookSourceInput.SetRx((float)atof(arg2));
				if (0 != _stricmp("*", arg3)) g_AfxHookSourceInput.SetRy((float)atof(arg3));
				if (0 != _stricmp("*", arg4)) g_AfxHookSourceInput.SetRz((float)atof(arg4));
				return;
			}

			Tier0_Msg(
				"mirv_input angles <yPitch> <xRoll> <zYaw> - Set new angles (only useful in camera input mode), use * where you don't want changes.\n"
				"Current value: %f %f %f\n"
				, g_Hook_VClient_RenderView.LastCameraAngles[0]
				, g_Hook_VClient_RenderView.LastCameraAngles[1]
				, g_Hook_VClient_RenderView.LastCameraAngles[2]
			);
			return;
		}
		else
		if(0 == _stricmp("fov", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if (4 <= argc) {
					char const * arg3 = args->ArgV(3);

					if (0 == _stricmp("real", arg2)) {
						g_AfxHookSourceInput.SetFov((float)Auto_InverseFovScaling(g_Hook_VClient_RenderView.LastWidth, g_Hook_VClient_RenderView.LastHeight, atof(arg3)));
						return;
					}
				}
				else {
					g_AfxHookSourceInput.SetFov((float)atof(arg2));
					return;
				}
			}

			Tier0_Msg(
				"mirv_input fov [real] <fov> - Set new fov (only useful in camera input mode).\n"
				"Current value: %f\n"
				, g_Hook_VClient_RenderView.LastCameraFov
			);
			return;
		}
		else if (0 == _stricmp("mem", arg1))
		{
			CSubWrpCommandArgs subArgs(args, 2);

			g_MirvInputMem.Console(&subArgs);

			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_input camera - Enable camera input mode, see HLAE manual for keys etc.\n"
		"mirv_input cfg [...] - Control input mode configuration.\n"
		"mirv_input end - End input mode(s).\n"
		"mirv_input position [...]\n"
		"mirv_input angles [...]\n"
		"mirv_input fov [...]\n"
		"mirv_input mem [...] - Store, use, save and load mirv_input view states.\n"
	);
}

CON_COMMAND(mirv_snd_timescale, "(CS:GO only) allows to override host_timescale value for sound system.")
{
	if(!Hook_csgo_SndMixTimeScalePatch())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		const char * arg1 = args->ArgV(1);

		if(0 == _stricmp("default", arg1))
		{
			csgo_SndMixTimeScalePatch_enable = false;
			return;
		}
		else
		{
			csgo_SndMixTimeScalePatch_enable = true;
			csgo_SndMixTimeScalePatch_value = (float)atof(arg1);
			return;
		}
	}
	Tier0_Msg(
		"Usage:\n"
		"mirv_snd_timescale <fValue> - override sound system host_timescale value with floating point value <fValue>.\n"
		"mirv_snd_timescale default - don't override.\n"
	);
}

CON_COMMAND(mirv_gameoverlay, "GameOverlayRenderer control.")
{
	int argc = args->ArgC();

	if(3 <= argc)
	{
		const char * arg1 = args->ArgV(1);

		if( 0 == _stricmp("enabled", arg1)
			|| 0 == _stricmp("enable", arg1))
		{
			bool value = 0 != atoi(args->ArgV(2));
			Tier0_Msg(
				"%s %s.\n",
				value ? "Enable" : "Disable",
				GameOverlay_Enable(value) ? "OK" : "FAILED"
			);
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_gameoverlay enabled 0|1 - Disable/Enable the GameOverlay (will only do s.th. useful when it was enabled initially).\n"
	);
}

CON_COMMAND(mirv_snd_filter, "Sound control (i.e. blocking sounds).")
{
	if(!csgo_S_StartSound_Install())
	{
		Tier0_Warning("Error: Hook not installed.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(0 == _stricmp("block", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				csgo_S_StartSound_Block_Add(arg2);
				return;
			}
		}
		else
		if(0 == _stricmp("print", arg1))
		{
			csgo_S_StartSound_Block_Print();
			return;
		}
		else
		if(0 == _stricmp("remove", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				csgo_S_StartSound_Block_Remove(atoi(arg2));
				return;
			}
		}
		else
		if(0 == _stricmp("clear", arg1))
		{
			csgo_S_StartSound_Block_Clear();
			return;
		}
		else
		if(0 == _stricmp("debug", arg1))
		{
			if(3 <= argc)
			{
				const char * arg2 = args->ArgV(2);

				g_csgo_S_StartSound_Debug = 0 != atoi(arg2);
				return;
			}
			
			Tier0_Msg("Current value: %s\n", g_csgo_S_StartSound_Debug ? "1" : "0");
			return;
		}
	}

	Tier0_Msg(
		"Usage:\n"
		"mirv_snd_filter block <mask> - Blocks given <mask> string (for format see below).\n"
		"mirv_snd_filter print - Prints current blocks.\n"
		"mirv_snd_filter remove <index> - Removes the block with index <index> (You can get that from the print sub-command).\n"
		"mirv_snd_filter clear - Clears all blocks.\n"
		"mirv_snd_filter debug 0|1 - Print sounds played into console.\n"
		"<mask> - string to match, where \\* = wildcard and \\\\ = \\\n"
	);
}

typedef struct MirvListEntitiesEntry_s {
	int idx;
	double dist;
	SOURCESDK::C_BaseEntity_csgo * be;
	
	MirvListEntitiesEntry_s(int _idx, double _dist, SOURCESDK::C_BaseEntity_csgo * _be)
		: idx(_idx)
		, dist(_dist)
		, be(_be)
	{

	}

} MirvListEntitiesEntry_t;

bool mirv_listentities_dist_compare(const MirvListEntitiesEntry_t & first, const MirvListEntitiesEntry_t & second)
{
	return first.dist < second.dist;
}

void mirv_print_entity(int index, double dist, SOURCESDK::C_BaseEntity_csgo * be)
{
	const char * className = be->GetClassname();
	if (!className) className = "[NULL]";

	const char * entName = be->GetEntityName();
	if (!entName) entName = "[NULL]";

	Tier0_Msg(
		"%i (%f): %s::%s :%i\n"
		, index
		, dist
		, className
		, entName
		, be->GetRefEHandle().ToInt()
	);

	if (be->IsPlayer() && g_VEngineClient)
	{
		if (SOURCESDK::IVEngineClient_014_csgo * pEngineCsgo = g_VEngineClient->GetVEngineClient_csgo())
		{

			SOURCESDK::player_info_t_csgo pInfo;
			if (pEngineCsgo->GetPlayerInfo(index, &pInfo))
			{
				std::ostringstream oss;

				oss << pInfo.xuid;

				Tier0_Msg(
					"\tplayerInfo.xuid: %s\n"
					"\tplayerInfo.name: %s\n"
					"\tplayerInfo.userID: %i\n"
					"\tplayerInfo.guid: %s\n"
					, oss.str().c_str()
					, pInfo.name
					, pInfo.userID
					, pInfo.guid
				);
			}
		}
	}
}


CON_COMMAND(mirv_listentities, "Print info about currently active entites. (CS:GO only)")
{
	if(!SOURCESDK::g_Entitylist_csgo)
	{
		Tier0_Warning("Not supported for your engine!\n");
		return;
	}

	bool onlyPlayer = false;
	bool sortByDistance = false;
	const char * classWildCard = "\\*";
	
	for (int i = 1; i < args->ArgC(); ++i)
	{
		const char * argI = args->ArgV(i);
		size_t argILen = strlen(argI);

		if (0 == _stricmp(argI, "isPlayer=1"))
		{
			onlyPlayer = true;
		}
		else if (0 == _stricmp(argI, "sort=distance"))
		{
			sortByDistance = true;
		}
		else if (StringIBeginsWith(argI, "class="))
		{
			classWildCard = argI + strlen("class=");
		}
	}

	std::list<MirvListEntitiesEntry_t> result;

	Vector3 cameraOrigin(
		g_Hook_VClient_RenderView.LastCameraOrigin[0],
		g_Hook_VClient_RenderView.LastCameraOrigin[1],
		g_Hook_VClient_RenderView.LastCameraOrigin[2]
	);

	int imax = SOURCESDK::g_Entitylist_csgo->GetHighestEntityIndex();

	for (int i = 0; i <= imax; ++i)
	{
		SOURCESDK::IClientEntity_csgo * ce = SOURCESDK::g_Entitylist_csgo->GetClientEntity(i);
		SOURCESDK::C_BaseEntity_csgo * be = ce ? ce->GetBaseEntity() : 0;

		if (be)
		{
		

			if ((!onlyPlayer || be->IsPlayer()))
			{
				const char * className = be->GetClassname();
				if (!className) className = "[NULL]";

				if (StringWildCard1Matched(classWildCard, className))
				{

					SOURCESDK::Vector vEntOrigin = be->GetAbsOrigin();
					Vector3 entOrigin(vEntOrigin.x, vEntOrigin.y, vEntOrigin.z);

					double dist = (entOrigin - cameraOrigin).Length();

					result.emplace_back(i, dist, be);
				}
			}
		}
	}

	if(sortByDistance)
		result.sort(mirv_listentities_dist_compare);

	Tier0_Msg(
		"Results:\n"
		"index (distance): className::entityName :entityHandle\n"
	);


	for(std::list<MirvListEntitiesEntry_t>::iterator it = result.begin(); it != result.end(); ++it)
	{
		int i = it->idx;
		SOURCESDK::C_BaseEntity_csgo * be = it->be;
		double dist = it->dist;

		mirv_print_entity(i, dist, be);
	}

	Tier0_Msg(
		"\n"
		"Usage:\n"
		"mirv_listentities [isPlayer=1] [sort=distance] [class=<wildCardString(\\* = wildcard, \\\\ = \\)>]\n"
	);


}

extern SOURCESDK::CSGO::IEngineTrace * g_pClientEngineTrace;

class CMirvTraceFilter : public SOURCESDK::CSGO::CTraceFilter
{
public:
	CMirvTraceFilter(const SOURCESDK::CSGO::IHandleEntity *passentity);
	virtual bool ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask);

private:
	const SOURCESDK::CSGO::IHandleEntity *m_pPassEnt;
};

CMirvTraceFilter::CMirvTraceFilter(const SOURCESDK::CSGO::IHandleEntity *passedict)
{
	m_pPassEnt = passedict;
}

bool CMirvTraceFilter::ShouldHitEntity(SOURCESDK::CSGO::IHandleEntity *pHandleEntity, int contentsMask)
{
	return pHandleEntity && pHandleEntity != m_pPassEnt;
}

CON_COMMAND(mirv_traceentity, "Trace entity from current view position")
{
	if (!(SOURCESDK::g_Entitylist_csgo && CClientToolsCsgo::Instance() && g_pClientEngineTrace))
	{
		Tier0_Warning("Not supported for your engine / missing hooks,!\n");
		return;
	}

	double forward[3], right[3], up[3];

	double Rx = (g_Hook_VClient_RenderView.LastCameraAngles[0]);
	double Ry = (g_Hook_VClient_RenderView.LastCameraAngles[1]);
	double Rz = (g_Hook_VClient_RenderView.LastCameraAngles[2]);
	
	MakeVectors(Rz, Rx, Ry, forward, right, up);

	double Tx = (g_Hook_VClient_RenderView.LastCameraOrigin[0] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[0] - 0 * right[0] + 0 * up[0]);
	double Ty = (g_Hook_VClient_RenderView.LastCameraOrigin[1] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[1] - 0 * right[1] + 0 * up[1]);
	double Tz = (g_Hook_VClient_RenderView.LastCameraOrigin[2] + SOURCESDK_CSGO_MAX_TRACE_LENGTH * forward[2] - 0 * right[2] + 0 * up[2]);

	SOURCESDK::Vector vecAbsStart;
	vecAbsStart.Init((float)g_Hook_VClient_RenderView.LastCameraOrigin[0], (float)g_Hook_VClient_RenderView.LastCameraOrigin[1], (float)g_Hook_VClient_RenderView.LastCameraOrigin[2]);

	SOURCESDK::Vector vecAbsEnd;
	vecAbsEnd.Init((float)Tx, (float)Ty, (float)Tz);

	SOURCESDK::CSGO::Ray_t ray;
	ray.Init(vecAbsStart, vecAbsEnd);
	
	SOURCESDK::C_BaseEntity_csgo * localPlayer = reinterpret_cast<SOURCESDK::C_BaseEntity_csgo *>(CClientToolsCsgo::Instance()->GetClientToolsInterface()->GetLocalPlayer());
	CMirvTraceFilter traceFilter(localPlayer);

	SOURCESDK::CSGO::trace_t tr;

	g_pClientEngineTrace->TraceRay(ray, SOURCESDK_CSGO_MASK_ALL, &traceFilter, &tr);

	Tier0_Msg(
		"Result:\n"
		"index (distance): className::entityName :entityHandle\n"
	);


	if (tr.DidHit() && tr.m_pEnt)
	{
		SOURCESDK::Vector vecSE;

		SOURCESDK::CSGO::VectorSubtract(tr.endpos, vecAbsStart, vecSE);

		mirv_print_entity(tr.GetEntityIndex(), vecSE.LengthSqr(), tr.m_pEnt);
	}
}

CON_COMMAND(mirv_aim, "Aiming system control.")
{
	if(!g_Hook_VClient_RenderView.GetGlobals())
	{
		Tier0_Warning("Error: Required hooks missing.\n");
		return;
	}

	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * arg1 = args->ArgV(1);

		if(0 == _stricmp("active", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.Active = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim active 0|1 - Whether aiming is active (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.Active ? "1" : "0"
			);
			return;
		}
		else
		if(0 == _stricmp("softDeactivate", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.SoftDeactivate = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim softDeactivate 0|1 - Whether to support soft deactivation (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.SoftDeactivate ? "1" : "0"
			);
			return;
		}
		else if (0 == _stricmp("calcVecAng", arg1))
		{
			if (3 <= argc)
			{
				IMirvVecAngCalc * vecAng = g_MirvVecAngCalcs.GetByName(args->ArgV(2));

				if (!vecAng)
					Tier0_Warning("No vecAng calc \"%s\" exists.\n", args->ArgV(2));

				g_Aiming.Source_set(vecAng);

				return;
			}

			IMirvVecAngCalc * vecAng = g_Aiming.Source_get();

			Tier0_Msg(
				"mirv_aim finder <sClacVecAngName> - Calc to use as source (<sClacVecAngName> is name from mirv_calcs vecAng).\n"
				"Current value: %s\n"
				, vecAng ? "" : "(none)"
			);

			if (vecAng) { vecAng->Console_PrintBegin(); vecAng->Console_PrintEnd(); }
			Tier0_Msg("\n");

			return;
		}
		else if (0 == _stricmp("calcVecAngClear", arg1))
		{
			g_Aiming.Source_set(0);
		}
		else
		if(0 == _stricmp("entityIndex", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.EntityIndex = atoi(arg2);
				g_Aiming.RebuildCalc();
				return;
			}

			Tier0_Msg(
				"mirv_aim entityIndex <n> - Entity index to aim after (use mirv_listentities to get one). Use invalid index (i.e. -1) to deactivate re-targeting.\n"
				"Current value: %i\n"
				, g_Aiming.EntityIndex
			);

			return;
		}
		else
		if(0 == _stricmp("point", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("cam", arg2))
				{
					Vector3 offset(0, 0, 0);

					if(6 <= argc)
					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPoint(
						Vector3(g_Hook_VClient_RenderView.LastCameraOrigin[0],
							g_Hook_VClient_RenderView.LastCameraOrigin[1],
							g_Hook_VClient_RenderView.LastCameraOrigin[2])
						+ offset);
					return;
				}
				else
				if(0 == _stricmp("last", arg2))
				{
					Vector3 offset(0, 0, 0);

					if(6 <= argc)
					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPointFromLast(offset);
					return;
				}
				else
				if(0 == _stricmp("abs", arg2) && 6 <= argc)
				{
					Vector3 offset(0, 0, 0);

					{
						char const * arg3 = args->ArgV(3);
						char const * arg4 = args->ArgV(4);
						char const * arg5 = args->ArgV(5);

						offset.X = atof(arg3);
						offset.Y = atof(arg4);
						offset.Z = atof(arg5);
					}

					g_Aiming.TargetPoint(offset);
					return;
				}
			}

			Tier0_Msg(
				"mirv_aim point abs <fX> <fY> <fz> - Absolute point in world coordinates.\n"
				"mirv_aim point cam [<fOffsetX> <fOffsetY> <fOffsetZ>] - Aim at current camera position with optional offset in world coordinates.\n"
				"mirv_aim point last [<fOffsetX> <fOffsetY> <fOffsetZ>] - Aim at last target position with optional offset in world coordinates.\n"
			);
			return;
		}
		else
		if(0 == _stricmp("offset", arg1))
		{
			if(5 <= argc)
			{
				char const * arg2 = args->ArgV(2);
				char const * arg3 = args->ArgV(3);
				char const * arg4 = args->ArgV(4);

				g_Aiming.OffSet = Vector3(
					atof(arg2), atof(arg3), atof(arg4)
				);
				return;
			}

			Tier0_Msg(
				"mirv_aim offset <x> <y> <z> - Offset in target local space to aim at (3 floating point values).\n"
				"Current value: %f %f %f\n",
				g_Aiming.OffSet.X, g_Aiming.OffSet.Y, g_Aiming.OffSet.Z
			);
			return;
		}
		else
		if(0 == _stricmp("snapTo", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.SnapTo = 0 != atoi(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim snapTo 0|1 - Whether to aim non-soft (1) or not (0).\n"
				"Current value: %s\n",
				g_Aiming.SnapTo ? "1" : "0"
			);
			return;
		}
		else
		if(0 == _stricmp("velLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitVelocity = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim velLimit <fValue> - Max velocity (in angle degrees per second^1) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitVelocity
			);
			return;
		}
		else
		if(0 == _stricmp("accelLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitAcceleration = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim accelLimit <fValue> - Max acceleration (in angle degrees per second^2) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitAcceleration
			);
			return;
		}
/*
		else
		if(0 == _stricmp("jerkLimit", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				g_Aiming.LimitJerk = atof(arg2);
				return;
			}

			Tier0_Msg(
				"mirv_aim jerkLimit <fValue> - Max jerk (in angle degrees per second^3) possible (floating point value).\n"
				"Current value: %f\n",
				g_Aiming.LimitJerk
			);
			return;
		}
*/
		else
		if(0 == _stricmp("origin", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("net", arg2))
				{
					g_Aiming.Origin = Aiming::O_Net;
					g_Aiming.RebuildCalc();
					return;
				}
				else
				if(0 == _stricmp("view", arg2))
				{
					g_Aiming.Origin = Aiming::O_View;
					g_Aiming.RebuildCalc();
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Origin)
			{
			case Aiming::O_Net:
				curValue = "net";
				break;
			case Aiming::O_View:
				curValue = "view";
				break;
			}

			Tier0_Msg(
				"mirv_aim origin net|view - Target origin to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
		else
		if(0 == _stricmp("angles", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("net", arg2))
				{
					g_Aiming.Angles = Aiming::A_Net;
					g_Aiming.RebuildCalc();
					return;
				}
				else
				if(0 == _stricmp("view", arg2))
				{
					g_Aiming.Angles = Aiming::A_View;
					g_Aiming.RebuildCalc();
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Angles)
			{
			case Aiming::A_Net:
				curValue = "net";
				break;
			case Aiming::A_View:
				curValue = "view";
				break;
			}

			Tier0_Msg(
				"mirv_aim angles net|view - Target angles to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
		else
		if(0 == _stricmp("up", arg1))
		{
			if(3 <= argc)
			{
				char const * arg2 = args->ArgV(2);

				if(0 == _stricmp("input", arg2))
				{
					g_Aiming.Up = Aiming::U_Input;
					return;
				}
				else
				if(0 == _stricmp("world", arg2))
				{
					g_Aiming.Up = Aiming::U_World;
					return;
				}
			}

			char const * curValue = "[unknown]";
			switch(g_Aiming.Up)
			{
			case Aiming::U_Input:
				curValue = "input";
				break;
			case Aiming::U_World:
				curValue = "world";
				break;
			}

			Tier0_Msg(
				"mirv_aim up input|world - Camera up direction to use.\n"
				"Current value: %s\n",
				curValue
			);
			return;
		}
	}

	Tier0_Msg(
		"mirv_aim active [...] - Whether aiming is active.\n"
		"mirv_aim softDeactivate [...] - Whether to support soft deactivation (for snapTo 0).\n"
		"mirv_aim calcVecAng [...] - Source for target (overrides entityIndex, point, origin, angles).\n"
		"mirv_aim calcVecAngClear - Clears source for target (no target) (overrides entityIndex, point, origin, angles).\n"
		"mirv_aim entityIndex [...] - Entity index to aim after (use mirv_listentities to get one).\n"
		"mirv_aim point [...] - Point to aim after.\n"
		"mirv_aim origin [...] - Target origin to use.\n"
		"mirv_aim angles [...] - Target angles to use.\n"
		"mirv_aim offset [...] - Offset in target space to aim at.\n"
		"mirv_aim up [...] - How to determine the camera up direction.\n"
		"mirv_aim snapTo [...] - Whether to aim non-soft or soft.\n"
		"mirv_aim velLimit [...] - Max velocity possible (for snapTo 0).\n"
		"mirv_aim accelLimit [...] - Max acceleration possible (for snapTo 0).\n"
		// does not work atm // "mirv_aim jerkLimit [...] - Max jerk possible (for snapTo 0).\n"
	);
}

CON_COMMAND(mirv_cmd, "Command system (for scheduling commands).")
{
	int argc = args->ArgC();

	if(2 <= argc)
	{
		char const * subcmd = args->ArgV(1);

		if (!_stricmp("addTick", subcmd))
		{
			std::string cmds("");

			for (int i = 2; i < args->ArgC(); ++i)
			{
				if (2 < i) cmds.append(" ");

				cmds.append(args->ArgV(i));
			}

			g_CommandSystem.AddTick(
				cmds.c_str()
			);
			return;
		}
		else if(!_stricmp("add", subcmd))
		{
			std::string cmds("");

			for(int i = 2; i < args->ArgC(); ++i)
			{
				if(2 < i) cmds.append(" ");
				
				cmds.append(args->ArgV(i));
			}

			g_CommandSystem.Add(
				cmds.c_str()
			);
			return;
		}
		else if (!_stricmp("addAtTick", subcmd) && 3 <= args->ArgC())
		{
			std::string cmds("");

			for (int i = 3; i < args->ArgC(); ++i)
			{
				if (3 < i) cmds.append(" ");

				cmds.append(args->ArgV(i));
			}

			g_CommandSystem.AddAtTick(
				cmds.c_str(),
				atof(args->ArgV(2))
			);
			return;
		}
		else if (!_stricmp("addAtTime", subcmd) && 3 <= args->ArgC())
		{
			std::string cmds("");

			for (int i = 3; i < args->ArgC(); ++i)
			{
				if (3 < i) cmds.append(" ");

				cmds.append(args->ArgV(i));
			}

			g_CommandSystem.AddAtTime(
				cmds.c_str(),
				atof(args->ArgV(2))
			);
			return;
		}
		else if (0 == _stricmp("addCurves", subcmd))
		{
			CSubWrpCommandArgs subArgs(args, 2);

			g_CommandSystem.AddCurves(&subArgs);
			return;
		}
		else if(!_stricmp("enabled", subcmd))
		{
			if(3 <= argc)
			{
				g_CommandSystem.Enabled = 0 != atof(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"mirv_cmd enabled 0|1 - Disable / enable command system.\n"
				"Current value: %s\n"
				, g_CommandSystem.Enabled ? "1" : "0"
			);
			return;
		}
		else if(!_stricmp("clear", subcmd) && 2 == argc)
		{
			g_CommandSystem.Clear();

			return;
		}
		else if(!_stricmp("print", subcmd) && 2 == argc)
		{
			g_CommandSystem.Console_List();

			if(g_CommandSystem.Enabled)
				Tier0_Msg("Command system is enabled (mirv_cmd enabled is 1).\n");
			else
				Tier0_Msg("COMMAND SYSTEM IS NOT ENABLED (mirv_cmd enabled is 0).\n");

			return;
		}
		else if(!_stricmp("remove", subcmd) && 3 <= argc)
		{
			g_CommandSystem.Remove(atoi(args->ArgV(2)));
			return;
		}
		else if(!_stricmp("load", subcmd) && 3 == argc)
		{	
			std::wstring wideString;
			bool bOk = UTF8StringToWideString(args->ArgV(2), wideString)
				&& g_CommandSystem.Load(wideString.c_str())
			;

			Tier0_Msg("Loading command system: %s.\n", bOk ? "OK" : "ERROR");

			return;
		}
		else if(!_stricmp("save", subcmd) && 3 == argc)
		{	
			std::wstring wideString;
			bool bOk = UTF8StringToWideString(args->ArgV(2), wideString)
				&& g_CommandSystem.Save(wideString.c_str())
			;

			Tier0_Msg("Saving command system: %s.\n", bOk ? "OK" : "ERROR");

			return;
		}
		else if (0 == _stricmp("edit", subcmd) && 3 <= argc)
		{
			const char * subcmd2 = args->ArgV(2);

			if (0 == _stricmp("startTime", subcmd2))
			{
				g_CommandSystem.EditStart(4 <= argc ? atof(args->ArgV(3)) : std::nextafter(g_CommandSystem.GetLastTime(), g_CommandSystem.GetLastTime() + 1.0));

				return;
			}
			else if (0 == _stricmp("startTick", subcmd2))
			{
				g_CommandSystem.EditStartTick(4 <= argc ? atof(args->ArgV(3)) : (double)g_CommandSystem.GetLastTick());

				return;
			}
			else if (0 == _stricmp("start", subcmd2))
			{
				g_CommandSystem.EditStart(g_CommandSystem.GetLastTime());
				g_CommandSystem.EditStartTick(g_CommandSystem.GetLastTick());

				return;
			}
			else if (0 == _stricmp("cmd", subcmd2))
			{
				CSubWrpCommandArgs subArgs(args, 3);
				g_CommandSystem.EditCommand(&subArgs);
				return;
			}
		}
	}

	Tier0_Msg(
		"mirv_cmd enabled [...] - Control if command system is enabled (by default it is).\n"
		"mirv_cmd addTick [commandPart1] [commandPart2] ... [commandPartN] - Adds commands at the current tick.\n"
		"mirv_cmd add [commandPart1] [commandPart2] ... [commandPartN] - Adds commands at the current time.\n"
		"mirv_cmd addAtTick <iTick> [commandPart1] [commandPart2] ... [commandPartN] - Adds commands at the given tick.\n"
		"mirv_cmd addAtTime <fTime> [commandPart1] [commandPart2] ... [commandPartN] - Adds commands at the given time.\n"
		"mirv_cmd addCurves [...] - Allows to add animated commands.\n"
		"mirv_cmd edit start - Set current time and tick as start time / tick.\n"
		"mirv_cmd edit startTime [fTime] - Set start time, current if argument not given.\n"
		"mirv_cmd edit startTick [fTick] - Set start tick, current if argument not given.\n"
		"mirv_cmd edit cmd [...] - Edit a specific command.\n"
		"mirv_cmd clear - Removes all commands.\n"
		"mirv_cmd print - Prints commands / state.\n"
		"mirv_cmd remove <index> - Removes a command by its index.\n"
		"mirv_cmd load <fileName> - loads commands from the file (XML format)\n"
		"mirv_cmd save <fileName> - saves commands to the file (XML format)\n"
	);
	return;
}

extern int g_iForcePostDataUpdateChanged;

CON_COMMAND(mirv_fix, "Various fixes")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		if (!_stricmp("physicsMaxFps", cmd1))
		{
			if (!Hook_csgo_vphsyics_frametime_lowerlimit())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				csgo_vphysics_SetMaxFps(atof(cmd2));
				return;
			}

			Tier0_Msg(
				"mirv_fix physicsMaxFps <floatFPSval> - Set the FPS limit for physics.\n"
				"Current value: %f\n",
				csgo_vphysics_GetMaxFps()
			);
			return;
		}
		else
			if (!_stricmp("blockObserverTarget", cmd1))
		{
			if (!Hook_csgo_C_BasePlayer_RecvProxy_ObserverTarget())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_csgo_Block_C_BasePlayer_RecvProxy_ObserverTarget = 0 != atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix blockObserverTarget 0|1 - Fixes unwanted player switching i.e. upon bomb plant (blocks C_BasePlayer::RecvProxy_ObserverTarget).\n"
				"Current value: %i\n",
				g_csgo_Block_C_BasePlayer_RecvProxy_ObserverTarget ? 1 : 0
			);
			return;
		}
		else
		if (!_stricmp("oldDuckFix", cmd1))
		{
			if (!Hook_csgo_CCSGameMovement_DuckFix())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_Enable_csgo_CCSGameMovement_DuckFix = 0 != atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix oldDuckFix 0|1 - Can fix player stuck in duck for old demos.\n"
				"Current value: %i\n",
				g_Enable_csgo_CCSGameMovement_DuckFix ? 1 : 0
			);
			return;
		}
		else
		if (!_stricmp("playerAnimState", cmd1))
		{
			if (!Hook_csgo_PlayerAnimStateFix())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				Enable_csgo_PlayerAnimStateFix_set(atoi(cmd2));

				if(args->ArgC() >= 4)
					g_csgo_mystique_annimation_factor = (float)atof(args->ArgV(3));

				return;
			}

			Tier0_Msg(
				"mirv_fix playerAnimState 0|1|2|3- Fixes twitching of player arms, see https://github.com/advancedfx/advancedfx/wiki/Source%%3ASmoother-Demos , 0 - disabled, 1 - enabled, 2/3 - debug.\n"
				"Current value: %i\n",
				Enable_csgo_PlayerAnimStateFix_get()
			);
			return;
		}
		/*
		else if (0 == _stricmp("demoIndexTicks", cmd1))
		{
			if (!Hook_csgo_DemoFile())
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				g_bFixCDemoFileTicks = atoi(cmd2);
				return;
			}

			Tier0_Msg(
				"mirv_fix demoIndexTicks <iValue> - Set to 1 or greater to enable indexing (sane values are 1920 ticks or greater), set 0 or less to disable.\n"
				"Current value: %i\n",
				g_bFixCDemoFileTicks
			);
			return;
		}
		*/
		else if (0 == _stricmp("selectedPlayerGlow", cmd1) && 3 == argc)
		{
			if (!AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS))
			{
				Tier0_Warning("Error: Required hooks not installed.\n");
				return;
			}

			MdtMemBlockInfos mdtInfos;

			MdtMemAccessBegin((LPVOID)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS), 7 * sizeof(unsigned char), &mdtInfos);

			int iArg2 = atoi(args->ArgV(2));

			if (2 == iArg2)
			{
				// Always glow selected.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x90;
			}
			else if (0 != iArg2)
			{
				// Normal mode.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x75;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x05;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0x74;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x16;
			}
			else
			{
				// Never glow slected.
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 0) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 1) = 0x90;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 5) = 0xEB;
				*((unsigned char*)AFXADDR_GET(csgo_GlowCurrentPlayer_JMPS) + 6) = 0x16;
			}

			MdtMemAccessEnd(&mdtInfos);

			return;
		}
		else if (0 == _stricmp("forcePostDataUpdateChanged", cmd1))
		{
			if (3 <= argc)
			{
				g_iForcePostDataUpdateChanged = atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"mirv_fix forcePostDataUpdateChanged -1|<iEntityIndex>\n"
				"Current value: %i\n"
				, g_iForcePostDataUpdateChanged);

			return;
		}
	}

	Tier0_Msg(
		"mirv_fix physicsMaxFps [...] - Can raise the FPS limit for physics (i.e. rag dolls, so they don't freeze upon high host_framerate).\n"
		"mirv_fix blockObserverTarget [...] - Fixes unwanted player switching i.e. upon bomb plant (blocks C_BasePlayer::RecvProxy_ObserverTarget).\n"
		"mirv_fix oldDuckFix [...] - Can fix player stuck in duck for old demos.\n"
		"mirv_fix playerAnimState [...] - Fixes twitching of player arms/legs, see https://github.com/advancedfx/advancedfx/wiki/Source%3ASmoother-Demos\n"
		//"mirv_fix demoIndexTicks [...] - Tries to make backward skipping faster in demos.\n"
		"mirv_fix selectedPlayerGlow 0|1|2 - 1: Game default, 2: Always glow, 0 : never glow.\n"
		"mirv_fix forcePostDataUpdateChanged [...].\n"
	);
	return;
}

extern SOURCESDK::CSGO::vgui::IPanel * g_pVGuiPanel_csgo;
extern SOURCESDK::CSGO::vgui::ISurface *g_pVGuiSurface_csgo;

bool MirvFindVPanel(SOURCESDK::CSGO::vgui::VPANEL panel, char const * panelName, SOURCESDK::CSGO::vgui::VPANEL * outPanel)
{
	if (!outPanel)
		return false;

	if (!strcmp(panelName, g_pVGuiPanel_csgo->GetName(panel)))
	{
		*outPanel = panel;
		return true;
	}			

	for (int i = 0; i < g_pVGuiPanel_csgo->GetChildCount(panel); ++i)
	{
		if (MirvFindVPanel(g_pVGuiPanel_csgo->GetChild(panel, i), panelName, outPanel))
			return true;
	}

	return false;
}

bool MirvVPanelSetVisible(char const * panelName, bool visible)
{
	SOURCESDK::CSGO::vgui::VPANEL panel;

	if (MirvFindVPanel(g_pVGuiSurface_csgo->GetEmbeddedPanel(), panelName, &panel))
	{
		g_pVGuiPanel_csgo->SetVisible(panel, visible);
		return true;
	}

	return false;
}

void MirvVPanelOnCommand(SOURCESDK::CSGO::vgui::Panel * panel, char const * command)
{
	int * vtable = *(int**)panel;

	void * onCommand = (void *)vtable[96];

	__asm push command
	__asm mov ecx, panel
	__asm call onCommand
}

void MirvDoVPanelOnCommand(char const * panelName, char const * destinationModule, char const * panelCommand)
{
	if (!(g_pVGuiSurface_csgo && g_pVGuiPanel_csgo))
	{
		Tier0_Warning("Error: Missing dependencies.\n");
		return;
	}

	SOURCESDK::CSGO::vgui::VPANEL vpanel;

	if (MirvFindVPanel(g_pVGuiSurface_csgo->GetEmbeddedPanel(), panelName, &vpanel))
	{
		SOURCESDK::CSGO::vgui::Panel * panel = g_pVGuiPanel_csgo->GetPanel(vpanel, destinationModule);
		if (panel)
			MirvVPanelOnCommand(panel, panelCommand);
		else
			Tier0_Warning("Error: GetPanel failed for %s\n", panelName);
	}
	else
		Tier0_Warning("Error: Invalid panel name %s\n", panelName);
}

CON_COMMAND(mirv_vpanel, "VGUI Panel access")
{
	if (!(g_pVGuiSurface_csgo && g_pVGuiPanel_csgo))
	{
		Tier0_Warning("Error: Missing dependencies.\n");
		return;
	}

	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);


		if (!_stricmp("hide", cmd1))
		{
			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (!MirvVPanelSetVisible(cmd2, false))
				{
					Tier0_Warning("Error: Invalid panel name %s\n", cmd2);
				}		
				return;
			}

			Tier0_Msg(
				"mirv_vpanel hide <panelName>\n"
			);
			return;
		}
		else
		if (!_stricmp("show", cmd1))
		{
			if (3 <= argc)
			{
				char const * cmd2 = args->ArgV(2);

				if (!MirvVPanelSetVisible(cmd2, true))
				{
					Tier0_Warning("Error: Invalid panel name %s\n", cmd2);
				}
				return;
			}

			Tier0_Msg(
				"mirv_vpanel show <panelName>\n"
			);
			return;
		}
		else if (!_stricmp("command", cmd1))
		{
			if (5 <= argc)
			{
				char const * panelName = args->ArgV(2);
				char const * panelModule = args->ArgV(3);
				char const * panelCommand = args->ArgV(4);

				MirvDoVPanelOnCommand(panelName, panelModule, panelCommand);
				return;
			}

			Tier0_Msg(
				"mirv_vpanel command <panelName> <sModule> <comand> - Execute <command> on panel with name <panelName> in module <sModule> (options are case-sensitive, <sModule> can e.g. be BaseUI or ClientDLL).\n"
				"Example: mirv_vpanel command DemoUIPanel BaseUI pause\n"
			);
			return;
		}
	}

	Tier0_Msg(
		"mirv_vpanel hide [...]\n"
		"mirv_vpanel show [...]\n"
		"mirv_vpanel command [...]\n"
		"Hint: To find panel names use vgui_drawtree 1.\n"
	);
}

CON_COMMAND(mirv_camio, "New camera motion data import / export.")
{
	g_Hook_VClient_RenderView.Console_CamIO(args);
}

CON_COMMAND(mirv_loadlibrary, "Load a DLL.")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		char const * cmd1 = args->ArgV(1);

		std::wstring wCmd1;
		if (UTF8StringToWideString(cmd1, wCmd1))
		{

			if (0 != LoadLibraryW(wCmd1.c_str()))
			{
				Tier0_Msg("LoadLibraryA OK.\n");
			}
			else
			{
				Tier0_Warning("LoadLibraryA failed.\n");
			}
		}
		else
		{
			Tier0_Warning("Failed to convert \"%s\" from UFT8 to UTF-16.\n", cmd1);
		}

		return;
	}

	Tier0_Msg(
		"mirv_loadlibrary <sDllFilePath> - Load DLL at given path.\n"
	);
}

CON_COMMAND(mirv_time, "time control")
{
	int argc = args->ArgC();

	if (2 <= argc)
	{
		const char * cmd1 = args->ArgV(1);

		if (0 == _stricmp("mode", cmd1))
		{
			if(3 <= argc)
			{
				const char * cmd2 = args->ArgV(2);

				g_MirvTime.SetMode(0 == _stricmp("resumePaused", cmd2) ? CMirvTime::TimeMode_ResumePaused : CMirvTime::TimeMode_CurTime);
				return;
			}

			Tier0_Msg(
				"mirv_time mode curTime|resumePaused - Time mode, curTime pauses when the demo pauses, resumePaused resumes even then. Default is curTime.\n"
				"Current value: %s\n"
				, CMirvTime::TimeMode_ResumePaused == g_MirvTime.GetMode() ? "resumePaused" : "curTime"
			);
			return;
		}
		else if (0 == _stricmp("pausedTime", cmd1))
		{
			if (3 <= argc)
			{
				const char * cmd2 = args->ArgV(2);

				g_MirvTime.SetPausedTime((float)atof(cmd2));
				return;
			}

			Tier0_Msg(
				"mirv_time pausedTime <fSeconds> - Set currently added paused time.\n"
				"Current value: %f\n"
				, g_MirvTime.GetPausedTime()
			);
			return;
		}
		else if (0 == _stricmp("drive", cmd1))
		{
			if (3 <= argc)
			{
				const char* cmd2 = args->ArgV(2);

				if (0 == _stricmp("default", cmd2))
				{
					g_MirvTime.SetDriveTimeEnabled(false);
				}
				else
				{

					float value = (float)atof(cmd2);
					float minValue = AFX_MATH_EPS;
					float maxValue = 1.0f / AFX_MATH_EPS;

					if (value < minValue)
					{
						value = minValue;
						Tier0_Warning("AFXWARNING: Limiting to minimum: %f\n.", minValue);
					}
					else if (maxValue < value)
					{
						value = maxValue;
						Tier0_Warning("AFXWARNING: Limiting to maximum: %f\n.", maxValue);
					}

					g_MirvTime.SetDriveTimeFactor(value);
					g_MirvTime.SetDriveTimeEnabled(true);
				}
				return;
			}

			Tier0_Msg(
				"mirv_time drive <fFactor> - Set the time drive factor.\n"
				"mirv_time drive default - Disable time drive.\n"
				"Current value: "
			);
			if (g_MirvTime.GetDriveTimeEnabled()) Tier0_Msg("%f\n", g_MirvTime.GetDriveTimeFactor());
			else Tier0_Msg("default\n");
			return;
		}
	}

	Tier0_Msg(
		"mirv_time mode [...]\n"
		"mirv_time pausedTime [...]\n"
		"mirv_time drive [...]\n"
	);
}


CON_COMMAND(mirv_cfg, "general HLAE configuration")
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (2 <= argC)
	{
		const char * arg1 = args->ArgV(1);

		if (0 == _stricmp("fovScaling", arg1))
		{
			CSubWrpCommandArgs subArgs(args, 2);
			Console_MirvFovScaling(&subArgs);
			return;
		}
		else if (0 == _stricmp("forceViewOverride", arg1))
		{
			if (3 <= argC)
			{
				g_Hook_VClient_RenderView.ForceViewOverride = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s forceViewOverride 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_Hook_VClient_RenderView.ForceViewOverride ? 1 : 0
			);
			return;
		}
		else if (0 == _stricmp("viewOverrideReset", arg1))
		{
			if (3 <= argC)
			{
				g_Hook_VClient_RenderView.ViewOverrideReset = 0 != atoi(args->ArgV(2));
				return;
			}

			Tier0_Msg(
				"%s viewOverrideReset 0|1\n"
				"Current value: %i\n"
				, arg0
				, g_Hook_VClient_RenderView.ViewOverrideReset ? 1 : 0
			);
			return;
		}
	}

	Tier0_Msg(
		"%s fovScaling [...] - Set default fov scaling.\n"
		"%s forceViewOverride [...] - If to force the view override onto the local player, can fix a few bugs (CS:GO only).\n"
		"%s viewOverrideReset [...] - If to reset roll to 0 and fov to 90 (unscaled) after ending a view override (CS:GO only).\n"
		, arg0
		, arg0
		, arg0
	);
}

CON_COMMAND(mirv_guides, "Draw guides on screen (CS:GO).")
{
	int argC = args->ArgC();
	const char * arg0 = args->ArgV(0);

	if (3 <= argC)
	{
		const char * arg1 = args->ArgV(1);
		const char * arg2 = args->ArgV(2);

		if (0 == _stricmp("enabled", arg2))
		{
			if (0 == _stricmp("phiGrid", arg1))
			{
				if (4 <= argC)
				{
					g_AfxStreams.DrawPhiGrid = 0 != atoi(args->ArgV(3));
					return;
				}

				Tier0_Msg(
					"%s phiGrid enabled 0|1\n"
					"Current value: %i\n"
					, arg0
					, g_AfxStreams.DrawPhiGrid ? 1 : 0
				);
				return;
			}
			else if (0 == _stricmp("ruleOfThirds", arg1))
			{
				if (4 <= argC)
				{
					g_AfxStreams.DrawRuleOfThirds = 0 != atoi(args->ArgV(3));
					return;
				}

				Tier0_Msg(
					"%s ruleOfThirds enabled 0|1\n"
					"Current value: %i\n"
					, arg0
					, g_AfxStreams.DrawRuleOfThirds ? 1 : 0
				);
				return;
			}
		}
	}

	Tier0_Msg(
		"%s phiGrid enabled [...]\n"
		"%s ruleOfThirds enabled [...]\n"
		, arg0
		, arg0
	);
}
