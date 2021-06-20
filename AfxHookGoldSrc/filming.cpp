#include "stdafx.h"

#include "filming.h"

#include <limits>

#include <iomanip>
#include <list>
#include <sstream>

#include <shared/AfxDetours.h>
#include <shared/FileTools.h>
#include <shared/StringTools.h>
#include <shared/RawOutput.h>
#include <shared/OpenExrOutput.h>

#include <hlsdk.h>
#include <deps/release/halflife/common/r_studioint.h>
#include <deps/release/halflife/common/demo_api.h>

#include "hooks/user32Hooks.h"
#include "hooks/hw/Mod_LeafPvs.h"
#include "hooks/hw/R_DrawEntitiesOnList.h"
#include "hooks/hw/R_DrawParticles.h"
#include "hooks/hw/R_DrawViewModel.h"
#include "hooks/hw/R_RenderView.h"
#include "hooks/client/cstrike/CrossHairFix.h"
#include "mirv_time.h"

#include "AfxSettings.h"
#include "supportrender.h"
#include "camimport.h"
#include "camexport.h"
#include "cmdregister.h"
#include "hl_addresses.h"
#include "mirv_glext.h"


using namespace std;


extern cl_enginefuncs_s *pEngfuncs;
extern engine_studio_api_s *pEngStudio;
extern playermove_s *ppmove;

extern float clamp(float, float, float);

REGISTER_DEBUGCVAR(camera_test, "0", 0);
REGISTER_DEBUGCVAR(debug_quat, "0", 0);
REGISTER_DEBUGCVAR(depth_bpp, "8", 0);
REGISTER_DEBUGCVAR(depth_slice_lo, "0.0", 0);
REGISTER_DEBUGCVAR(depth_slice_hi, "1.0", 0);
REGISTER_DEBUGCVAR(sample_frame_strength, "1.0", 0);
REGISTER_DEBUGCVAR(sample_smethod, "1", 0);
REGISTER_DEBUGCVAR(print_frame, "0", 0);
REGISTER_DEBUGCVAR(print_pos, "0", 0);

REGISTER_CVAR(camexport_mode, "0", 0);
REGISTER_CVAR(crop_height, "-1", 0);
REGISTER_CVAR(crop_yofs, "-1", 0);
REGISTER_CVAR(depth_exr, "0", 0);
REGISTER_CVAR(depth_streams, "3", 0);
REGISTER_CVAR(fx_lightmap, "0", 0);
REGISTER_CVAR(fx_viewmodel_lightmap, "0", 0);
REGISTER_CVAR(fx_wh_enable, "0", 0);
REGISTER_CVAR(fx_wh_alpha, "0.5", 0);
REGISTER_CVAR(fx_wh_additive, "1", 0);
REGISTER_CVAR(fx_wh_noquads, "0", 0);
REGISTER_CVAR(fx_wh_tint_enable, "0", 0);
REGISTER_CVAR(fx_wh_xtendvis, "1", 0);
REGISTER_CVAR(fx_xtendvis, "0", 0);
REGISTER_CVAR(tas_mode, "0", 0);

REGISTER_CVAR(matte_entityquads, "2", 0);
REGISTER_CVAR(matte_method, "1", 0);
REGISTER_CVAR(matte_particles, "2", 0);
REGISTER_CVAR(matte_viewmodel, "2", 0);
REGISTER_CVAR(matte_worldmodels, "1", 0);
REGISTER_CVAR(matte_xray, "0", 0);

REGISTER_CVAR(movie_clearscreen, "0", 0);
REGISTER_CVAR(movie_bmp, "1", 0);
REGISTER_CVAR(movie_depthdump, "0", 0);
REGISTER_CVAR(movie_export_sound, "0", 0); // should default to 1, but I don't want to mess up other updates
REGISTER_CVAR(movie_filename, "untitled_rec", 0);
REGISTER_CVAR(movie_fps, "30", 0);
REGISTER_CVAR(movie_hidepanels, "0", 0);
REGISTER_CVAR(movie_playdemostop,"0",0);
REGISTER_CVAR(movie_separate_hud, "0", 0);
REGISTER_CVAR(movie_simulate, "0", 0);
REGISTER_CVAR(movie_simulate_delay, "0", 0);
REGISTER_CVAR(movie_sound_volume, "0.4", 0); // volume 0.8 is CS 1.6 default
REGISTER_CVAR(movie_sound_extra, "0", 0);
REGISTER_CVAR(movie_stereomode,"0",0);
REGISTER_CVAR(movie_stereo_centerdist,"1.3",0);
REGISTER_CVAR(movie_stereo_yawdegrees,"0.0",0);
REGISTER_CVAR(movie_splitstreams, "0", 0);
REGISTER_CVAR(movie_wireframe, "0", 0);
REGISTER_CVAR(movie_wireframesize, "1", 0);
REGISTER_CVAR(sample_enable, "0", 0);
REGISTER_CVAR(sample_exposure, "1.0", 0);
REGISTER_CVAR(sample_sps, "180", 0);


// Our filming singleton
Filming g_Filming;

FilmingStream * g_Filming_Stream[13];

enum FilmingStreamSlot {
	FS_all = 0, FS_all_right = 1,
	FS_world = 2, FS_world_right = 3,
	FS_entity = 4, 	FS_entity_right = 5,
	FS_depthall = 6, FS_depthall_right = 7,
	FS_depthworld = 8, FS_depthworld_right = 9,
	FS_hudcolor = 10, FS_hudalpha = 11,
	FS_debug = 12
};

const wchar_t* g_DefaultFfmpegOptionsColor = L"-c:v libx264 -pix_fmt yuv420p -preset slow -crf 22 {QUOTE}{AFX_STREAM_PATH}\\\\video.mp4{QUOTE}";

std::wstring g_FilmingStream_FfmpegOptions[13] = {
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor, g_DefaultFfmpegOptionsColor,
	g_DefaultFfmpegOptionsColor
};

bool g_FilmingStream_FfmpegEnable[13] = {
	false, false,
	false, false,
	false, false,
	false, false,
	false, false,
	false, false,
	false
};

// from HL1SDK/multiplayer/common/mathlib.h:
#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif




//
// // // //
//

REGISTER_CMD_FUNC(cameraofs_cs)
{
	if (pEngfuncs->Cmd_Argc() == 4 || pEngfuncs->Cmd_Argc() == 7)
	{
		g_Filming.SetCameraOfs((float)atof(pEngfuncs->Cmd_Argv(1)), (float)atof(pEngfuncs->Cmd_Argv(2)), (float)atof(pEngfuncs->Cmd_Argv(3)));

		if(pEngfuncs->Cmd_Argc() == 7)
			g_Filming.SetCameraAngs((float)atof(pEngfuncs->Cmd_Argv(4)), (float)atof(pEngfuncs->Cmd_Argv(5)), (float)atof(pEngfuncs->Cmd_Argv(6)));
		else
			g_Filming.SetCameraAngs(0,0,0);
	}
	else
		pEngfuncs->Con_Printf("Usage: " PREFIX "cameraofs_cs <right> <up> <forward> [<pitch> <yaw> <roll>]\nNot neccessary for stereo mode, use mirv_movie_stereo instead\n");
}



//bool Filming::bNoMatteInterpolation()
//{ return _bNoMatteInterpolation; }

//void Filming::bNoMatteInterpolation (bool bSet)
//{ if (m_iFilmingState == FS_INACTIVE) _bNoMatteInterpolation = bSet; }


void do_camera_test(float vieworg[3], float viewangles[3]) {
	static unsigned int state = 0;
	static float angles[3];
	static float ofs[3];

	switch(state) {
	case 1:
		ofs[0] += 2.0f;
		if(360.0f <= ofs[0]) {
			state++;
			ofs[0] = 0;
		}
		break;
	case 2:
		ofs[1] += 2.0f;
		if(360.0f <= ofs[1]) {
			state++;
			ofs[1] = 0;
		}
		break;
	case 3:
		ofs[2] += 2.0f;
		if(360.0f <= ofs[2]) {
			state++;
			ofs[2] = 0;
		}
		break;
	case 4:
		angles[0] += 2.0f;
		if(360.0f <= angles[0]) {
			state++;
			angles[0] = 0;
		}
		break;
	case 5:
		angles[1] += 2.0f;
		if(360.0f <= angles[1]) {
			state++;
			angles[1] = 0;
		}
		break;
	case 6:
		angles[2] += 2.0f;
		if(360.0f <= angles[2]) {
			state++;
			angles[2] = 0;
		}
		break;
	case 7:
		angles[0] -= 2.0f;
		if(-360.0f >= angles[0]) {
			state++;
			angles[0] = 0;
		}
		break;
	case 8:
		angles[1] -= 2.0f;
		if(-360.0f >= angles[1]) {
			state++;
			angles[1] = 0;
		}
		break;
	case 9:
		angles[2] -= 2.0f;
		if(-360.0f >= angles[2]) {
			state++;
			angles[2] = 0;
		}
		break;
	default:
		angles[0] = 0;
		angles[1] = 0;
		angles[2] = 0;
		ofs[0] = 0;
		ofs[0] = 0;
		ofs[0] = 0;

		state = 1;
	};

	viewangles[0] = angles[0];
	viewangles[1] = angles[1];
	viewangles[2] = angles[2];
	vieworg[0] += ofs[0];
	vieworg[1] += ofs[1];
	vieworg[2] += ofs[2];

	if(state < 10) {
		static char szInfo[100];

		if(state < 4)
			_snprintf_s(szInfo, 100, _TRUNCATE, "ofs=(%f, %f, %f)", ofs[0], ofs[1], ofs[2]);
		else if(state < 10)
			_snprintf_s(szInfo, 100, _TRUNCATE, "ang=(%f, %f, %f)", angles[0], angles[1], angles[2]);

		pEngfuncs->pfnCenterPrint(szInfo);
	}
	else
		pEngfuncs->pfnCenterPrint("zZz");
}

void Filming::FovOverride(double value)
{
	m_FovValue = value;
	m_FovOverride = true;
}

void Filming::FovDefault()
{
	m_FovOverride = false;
}

void Filming::OnR_RenderView(float vieworg[3], float viewangles[3], float & fov)
{
	if(debug_quat->value)
	{
		Quaternion Q = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
			viewangles[PITCH],
			viewangles[YAW],
			viewangles[ROLL]
		)));

		QEulerAngles A = Q.ToQREulerAngles().ToQEulerAngles();

		viewangles[PITCH] = (float)A.Pitch;
		viewangles[YAW] = (float)A.Yaw;
		viewangles[ROLL] = (float)A.Roll;
	}

	if(m_FovOverride)
	{
		fov = (float)m_FovValue;
	}

	//
	// Camera spline interaction:

	if(m_CamPath.Enabled_get() && m_CamPath.CanEval())
	{
		double time = Mirv_GetDemoTimeOrClientTime();

		// no extrapolation:
		if(m_CamPath.GetLowerBound() <= time && time <= m_CamPath.GetUpperBound())
		{
			CamPathValue val = m_CamPath.Eval( time );
			QEulerAngles ang = val.R.ToQREulerAngles().ToQEulerAngles();

			vieworg[0] = (float)val.X;
			vieworg[1] = (float)val.Y;
			vieworg[2] = (float)val.Z;

			viewangles[PITCH] = (float)ang.Pitch;
			viewangles[YAW] = (float)ang.Yaw;
			viewangles[ROLL] = (float)ang.Roll;

			fov = (float)val.Fov;
		}
	}

	//
	// override by cammotion import:
	if(g_CamImport.IsActive())
	{
		static double dtmp[6];
	
		if(g_CamImport.GetCamPosition(g_Filming.GetDebugClientTime(),dtmp))
		{
			vieworg[1] = (float)-dtmp[0];
			vieworg[2] = (float)+dtmp[1];
			vieworg[0] = (float)-dtmp[2];
			viewangles[ROLL] = (float)-dtmp[3];
			viewangles[PITCH] = (float)-dtmp[4];
			viewangles[YAW] = (float)+dtmp[5];
		}
	}

	//
	// limit fov to sane values:

	if(fov<1) fov = 1;
	else if(fov>179) fov = 179;

	//
	// Apply rotation displacement:
	{
		float fPitch, fYaw, fRoll;
		g_Filming.GetCameraAngs(fPitch, fYaw, fRoll);

		if (0 != fPitch || 0 != fYaw || 0 != fRoll)
		{
			Quaternion quatOfs = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
				fPitch,
				fYaw,
				fRoll
			)));

			Quaternion quatEngine = Quaternion::FromQREulerAngles(QREulerAngles::FromQEulerAngles(QEulerAngles(
				viewangles[PITCH],
				viewangles[YAW],
				viewangles[ROLL]
			)));

			QEulerAngles A = (quatOfs * quatEngine).ToQREulerAngles().ToQEulerAngles();

			viewangles[PITCH] = (float)A.Pitch;
			viewangles[YAW] = (float)A.Yaw;
			viewangles[ROLL] = (float)A.Roll;
		}
	}

	// >> begin calculate transform vectors

	double forward[3],right[3],up[3];

	MakeVectors(viewangles[ROLL], viewangles[PITCH], viewangles[YAW], forward, right, up);

	// << end calculate transform vectors

	// (this code is similar to HL1SDK/multiplayer/cl_dll/view.cpp/V_CalcNormalRefdef)

	//
	// apply displacement :
	{
		float fDispRight, fDispUp, fDispForward;

		g_Filming.GetCameraOfs(fDispRight, fDispUp, fDispForward);

		for ( int i=0 ; i<3 ; i++ )
		{
			vieworg[i] += (float)(fDispForward*forward[i] + fDispRight*right[i] + fDispUp*up[i]);
		}
	}

	// camera test:
	if(camera_test->value)
		do_camera_test(vieworg, viewangles);

	// export:

	if(g_CamExport.HasFileMain()
		&& m_LastCamFrameMid != m_nFrames
	) {
		m_LastCamFrameMid = m_nFrames;

		g_CamExport.WriteMainFrame(
			-vieworg[1], +vieworg[2], -vieworg[0],
			-viewangles[ROLL], -viewangles[PITCH], +viewangles[YAW]
		);
	}

	//
	// Apply Stereo mode:

	// one note on the stereoyawing:
	// it may look simple, but it is only simple since the H-L engine carrys the yawing out as last rotation and after that translates
	// if this wouldn't be the case, then we would have a bit more complicated calculations to make sure the camera is always rotated around the up axis!

	if (g_Filming.bEnableStereoMode()&&(g_Filming.isFilming()))
	{
		float fDispRight;
		Filming::STEREO_STATE sts =g_Filming.GetStereoState(); 

		if(sts ==Filming::STS_LEFT)
		{
			// left
			fDispRight = movie_stereo_centerdist->value; // left displacement
			viewangles[YAW] -= movie_stereo_yawdegrees->value; // turn right
		}
		else
		{
			// right
			fDispRight = movie_stereo_centerdist->value; // right displacement
			viewangles[YAW] += movie_stereo_yawdegrees->value; // turn left
		}

		// apply displacement:
		for ( int i=0 ; i<3 ; i++ )
		{
			vieworg[i] += (float)(fDispRight*right[i]);
		}

		// export:
		if(sts == Filming::STS_LEFT
			&& g_CamExport.HasFileLeft()
			&& m_LastCamFrameLeft != m_nFrames
		) {
			m_LastCamFrameLeft = m_nFrames;

			g_CamExport.WriteLeftFrame(
				-vieworg[1], +vieworg[2], -vieworg[0],
				-viewangles[ROLL], -viewangles[PITCH], +viewangles[YAW]
			);
		}
		else if(g_CamExport.HasFileRight()
			&& m_LastCamFrameRight != m_nFrames
		) {
			m_LastCamFrameRight = m_nFrames;

			g_CamExport.WriteRightFrame(
				-vieworg[1], +vieworg[2], -vieworg[0],
				-viewangles[ROLL], -viewangles[PITCH], +viewangles[YAW]
			);
		}

	}

	LastCameraOrigin[0] = vieworg[0];
	LastCameraOrigin[1] = vieworg[1];
	LastCameraOrigin[2] = vieworg[2];
	LastCameraAngles[PITCH] = viewangles[PITCH];
	LastCameraAngles[YAW] = viewangles[YAW];
	LastCameraAngles[ROLL] = viewangles[ROLL];
	LastCameraFov = fov;

	if(print_pos->value!=0.0)
		pEngfuncs->Con_Printf("(%f,%f,%f) (%f,%f,%f)\n",
			vieworg[0],
			vieworg[1],
			vieworg[2],
			viewangles[PITCH],
			viewangles[YAW],
			viewangles[ROLL]
		);
}

void Filming::On_CL_Disconnect(void)
{
	if(!(g_Filming.isFilming()
		&& pEngfuncs->pDemoAPI->IsPlayingback()
		&& movie_playdemostop->value))
		return;


	pEngfuncs->Con_Printf("Auto stopping filming as requested (on CL_Disconnect) ...\n");
	g_Filming.Stop();
}

void Filming::SupplyZClipping(GLdouble zNear, GLdouble zFar) {
	m_ZNear = zNear;
	m_ZFar = zFar;
}



bool Filming::bEnableStereoMode()
{ return m_EnableStereoMode; }


void Filming::SetCameraOfs(float right, float up, float forward)
{
	_cameraofs.right = right;
	_cameraofs.up = up;
	_cameraofs.forward = forward;
}

void Filming::SetCameraAngs(float pitch, float yaw, float roll)
{
	_cameraofs.pitch = pitch;
	_cameraofs.yaw = yaw;
	_cameraofs.roll = roll;
}

void Filming::SetStereoOfs(float left_and_rightofs)
{
	_fStereoOffset = left_and_rightofs;
}

Filming::Filming()
// constructor
{
	m_bInWireframe = false;
	m_EnableStereoMode = false;

		_fStereoOffset = (float)1.27;
		
		// this is currently done globally: _detoured_R_RenderView = NULL; // only hook when requested

		_bRecordBuffers_FirstCall = true; //has to be set here cause it is checked by isFinished()

		// (will be set in Start again)
		_stereo_state = STS_LEFT;

		_HudRqState = HUDRQ_NORMAL;

		_bSimulate2 = false;

	_fx_whRGBf[0]=0.0f;
	_fx_whRGBf[1]=0.5f;	
	_fx_whRGBf[2]=1.0f;

	 bRequestingMatteTextUpdate=false;

	 matte_entities_r.bNotEmpty=false; // by default empty
}

Filming::~Filming()
// destructor
{
}

void Filming::DoCanDebugCapture()
{
	if(isFilming() && g_Filming_Stream[FS_debug]) g_Filming_Stream[FS_debug]->Capture(m_time, &m_GlRawPic, m_fps);
}

void Filming::GetCameraOfs(float& right, float& up, float& forward)
{
	right = _cameraofs.right;
	up = _cameraofs.up;
	forward = _cameraofs.forward;
}

void Filming::GetCameraAngs(float& pitch, float& yaw, float& roll)
{
	pitch = _cameraofs.pitch;
	yaw = _cameraofs.yaw;
	roll = _cameraofs.roll;
}

double Filming::GetDebugClientTime()
{
	if(m_iFilmingState != FS_INACTIVE)
		return m_StartClientTime + m_time;

	return pEngfuncs->GetClientTime();
}

Filming::MATTE_METHOD Filming::GetMatteMethod()
{
	return m_MatteMethod;
}

float Filming::GetStereoOffset()
{
	return _fStereoOffset;
}

Filming::STEREO_STATE Filming::GetStereoState()
{
	return _stereo_state;
}

Filming::HUD_REQUEST_STATE Filming::giveHudRqState()
{
	if (!isFilming())
		return HUDRQ_NORMAL;

	return _HudRqState;
}

Filming::MATTE_STAGE Filming::GetMatteStage()
{
	return m_iMatteStage;
}

void Filming::OnHudBeginEvent()
{
	//MessageBox(NULL,"IN","HUD Event",MB_OK);
	switch(giveHudRqState())
	{
	case HUDRQ_CAPTURE_COLOR:
		glClearColor(0.0f,0.0f,0.0f, 1.0f); // don't forget to set our clear color
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		break;
	case HUDRQ_CAPTURE_ALPHA:
		g_Cstrike_CrossHair_Block = true; // no cool-down now.
		glClearColor(0.0f,0.0f,0.0f, 0.0f); // don't forget to set our clear color
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		break;
	}
}

bool Filming::OnHudEndEvent()
{
	m_HudDrawnInFrame = true;

	//MessageBox(NULL,"OUT","HUD Event",MB_OK);
	switch(giveHudRqState())
	{
	case HUDRQ_CAPTURE_COLOR:
		if(g_Filming_Stream[FS_hudcolor]) g_Filming_Stream[FS_hudcolor]->Capture(m_time, &m_GlRawPic, m_fps);
		if(g_Filming_Stream[FS_hudalpha])
		{
			// we want alpha too in this case
			_HudRqState = HUDRQ_CAPTURE_ALPHA; // set ALPHA mode!
			return true; // we need loop
		}
		break;
	case HUDRQ_CAPTURE_ALPHA:
		if(g_Filming_Stream[FS_hudalpha]) g_Filming_Stream[FS_hudalpha]->Capture(m_time, &m_GlRawPic, m_fps);
		g_Cstrike_CrossHair_Block = false; // allow cool-down again.
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); // restore default color mask again!
		break;
	}

	if(m_CaptureEarly && isFilming() && m_iFilmingState != FS_STARTING)
	{
		if(MS_ALL == m_iMatteStage)
		{
			if(g_Filming_Stream[FS_all]) g_Filming_Stream[FS_all]->Capture(m_time, &m_GlRawPic, m_fps);
			if(g_Filming_Stream[FS_depthall]) g_Filming_Stream[FS_depthall]->Capture(m_time, &m_GlRawPic, m_fps);
		}
		else if(MS_WORLD == m_iMatteStage)
		{
			if(g_Filming_Stream[FS_world]) g_Filming_Stream[FS_world]->Capture(m_time, &m_GlRawPic, m_fps);
			if(g_Filming_Stream[FS_depthworld]) g_Filming_Stream[FS_depthworld]->Capture(m_time, &m_GlRawPic, m_fps);
		}
	}

	return false; // do not loop
}


void Filming::setScreenSize(GLint w, GLint h)
{
	if (m_Width < w) m_Width = w;
	if (m_Height < h) m_Height = h;
}



void Filming::Start()
{
	m_HostFrameCount = 0;
	m_StartTickCount = GetTickCount();

	_bSimulate2 = (movie_simulate->value != 0.0);

	// Prepare directories:

	if(!_bSimulate2)
	{
		if(AnsiStringToWideString(movie_filename->string, m_TakeDir)
			&& (m_TakeDir.append(L"\\take"), SuggestTakePath(m_TakeDir.c_str(), 4, m_TakeDir))
			&& CreatePath(m_TakeDir.c_str(), m_TakeDir)
		)
		{
			std::string ansiTakeDir;

			pEngfuncs->Con_Printf("Recording to \"%s\".\n", WideStringToAnsiString(m_TakeDir.c_str(), ansiTakeDir) ? ansiTakeDir.c_str() : "?");

		}
		else
		{
			pEngfuncs->Con_Printf("Error: Failed to create directories for \"%s\".\n", movie_filename->string);
			return; //abort
		}
	}
	else
	{
		pEngfuncs->Con_Printf("Recording (simulated).");
	}

	if(g_AfxSettings.OptimizeCaptureVis_get())
		UndockGameWindowForCapture();

	m_MatteMethod = 2 == matte_method->value ? MM_ALPHA : MM_KEY;

	if(MM_ALPHA == m_MatteMethod && !g_Has_GL_ARB_multitexture)
	{
		pEngfuncs->Con_Printf("ERROR: Alpha mattes not supported by your setup.\n");
		m_MatteMethod = MM_KEY;
	}

	bool enableSampling = 0 != sample_enable->value;

	m_fps = enableSampling ? max(sample_sps->value, 1.0f) :  max(movie_fps->value, 1.0f);

	m_time = 0.0;
	m_LastHostTime = m_time;

	m_TASMode = (tas_mode->value != 0.0f);

	if (_pSupportRender)
		_pSupportRender->hlaeOnFilmingStart();	

	m_nFrames = 0;
	
	m_LastCamFrameMid = -1;
	m_LastCamFrameLeft = -1;
	m_LastCamFrameRight = -1;

	m_StartClientTime = pEngfuncs->GetClientTime();
	m_iFilmingState = FS_STARTING;
	m_iMatteStage = MS_WORLD;

	_fStereoOffset = movie_stereo_centerdist->value;
	m_EnableStereoMode = (movie_stereomode->value != 0.0); // we also have to be able to use R_RenderView


	// Mod_LeafPvs (WallHack related):
	g_Mod_LeafPvs_NoVis = ( fx_wh_enable->value == 0.0f && fx_xtendvis->value != 0.0f)||(fx_wh_enable->value != 0.0f && fx_wh_xtendvis->value != 0.0f);


	// setup camexport:
	if (!_bSimulate2) {
		float fc = camexport_mode->value;
		double frameTime = 1.0 / m_fps;

		if(fc && fc != 2.0f) g_CamExport.BeginFileMain(m_TakeDir.c_str(), frameTime);
		if(fc && fc >= 2.0f) g_CamExport.BeginFileLeft(m_TakeDir.c_str(), frameTime);
		if(fc && fc >= 2.0f) g_CamExport.BeginFileRight(m_TakeDir.c_str(), frameTime);
	}

	// make sure some states used in recordBuffers are set properly:
	_HudRqState = HUDRQ_NORMAL;
	_stereo_state = STS_LEFT;
	_bRecordBuffers_FirstCall = true;


	// prepare (and force) some HL engine settings:

	// Clear up the screen
	if (movie_clearscreen->value != 0.0f)
	{
		pEngfuncs->Cvar_SetValue("hud_draw", 0);
		pEngfuncs->Cvar_SetValue("crosshair", 0);
	}

	// indicate sound export if requested:
	_bExportingSound = !_bSimulate2 && (movie_export_sound->value!=0.0f);	

	// Prepare streams:
	{
		double samplingFrameDuration = enableSampling ? 1.0 / (double)(max(movie_fps->value, 1.0f)) : 0.0;

		int x = 0;
		int y = (int)crop_yofs->value;
		int width = m_Width;
		int height = (int)crop_height->value;

		if (height == -1)
			height = m_Height; // default height
		else
		{
			// limit height:

			if (height > m_Height) height=m_Height;
			else if (height < 2)  height=2;
		}

		if (y == -1)
			y = (m_Height-height)/2; // default ofs.
		else {
			// limit ofs:

			int yD = (m_Height - height);

			// user specified an offset, we will transform the values for him, so he sees Yofs/-axis as top->down while OpenGL handles it down->up
			y  =  yD - y; //GL y-axis is down->up

			if (y > yD) y=yD;
			else if (y<0) y=0;
		}

		bool bWorld  = 1 == (int)movie_splitstreams->value || 3 == (int)movie_splitstreams->value;
		bool bEntity = 2 == (int)movie_splitstreams->value || 3 == (int)movie_splitstreams->value;
		bool bAll    = 0 == (int)movie_splitstreams->value;

		bool bDepthWorld = bWorld && movie_depthdump->value && (1 == (int)depth_streams->value || 3 <= (int)depth_streams->value);
		bool bDepthAll   = (bAll || bEntity) && movie_depthdump->value && (2 == (int)depth_streams->value || 3 <= (int)depth_streams->value);

		bool bHudColor = 0 != movie_separate_hud->value;
		bool bHudAlpha = bHudColor && 2 != movie_separate_hud->value;

		wchar_t const * takePath = m_TakeDir.c_str();

		g_Filming_Stream[FS_all] = 0;
		g_Filming_Stream[FS_all_right] = 0;
		g_Filming_Stream[FS_world] = 0;
		g_Filming_Stream[FS_world_right] = 0;
		g_Filming_Stream[FS_entity] = 0;
		g_Filming_Stream[FS_entity_right] = 0;
		g_Filming_Stream[FS_depthall] = 0;
		g_Filming_Stream[FS_depthall_right] = 0;
		g_Filming_Stream[FS_depthworld] = 0;
		g_Filming_Stream[FS_depthworld_right] = 0;
		g_Filming_Stream[FS_hudcolor] = 0;
		g_Filming_Stream[FS_hudalpha] = 0;
		g_Filming_Stream[FS_debug] = 0;

		if(bAll)
		{
			g_Filming_Stream[FS_all] = new FilmingStream(
				takePath, !m_EnableStereoMode ? L"all" : L"all_left",
				FB_COLOR,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_all] ? g_FilmingStream_FfmpegOptions[FS_all].c_str() : nullptr
			);

			if(m_EnableStereoMode)
			{
				g_Filming_Stream[FS_all_right] = new FilmingStream(
					takePath, L"all_right",
					FB_COLOR,
					samplingFrameDuration,
					m_TASMode,
					x, y, width, height
					, g_FilmingStream_FfmpegEnable[FS_all_right] ? g_FilmingStream_FfmpegOptions[FS_all_right].c_str() : nullptr
				);
			}
		}

		if(bWorld)
		{
			g_Filming_Stream[FS_world] = new FilmingStream(
				takePath, !m_EnableStereoMode ? L"world" : L"world_left",
				FB_COLOR,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_world] ? g_FilmingStream_FfmpegOptions[FS_world].c_str() : nullptr
			);

			if(m_EnableStereoMode)
			{
				g_Filming_Stream[FS_world_right] = new FilmingStream(
					takePath, L"world_right",
					FB_COLOR,
					samplingFrameDuration,
					m_TASMode,
					x, y, width, height
					, g_FilmingStream_FfmpegEnable[FS_world_right] ? g_FilmingStream_FfmpegOptions[FS_world_right].c_str() : nullptr
				);
			}
		}

		if(bEntity)
		{
			g_Filming_Stream[FS_entity] = new FilmingStream(
				takePath, !m_EnableStereoMode ? L"entity" : L"entity_left",
				FB_COLOR,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_entity] ? g_FilmingStream_FfmpegOptions[FS_entity].c_str() : nullptr
			);

			if(m_EnableStereoMode)
			{
				g_Filming_Stream[FS_entity_right] = new FilmingStream(
					takePath, L"entity_right",
					FB_COLOR,
					samplingFrameDuration,
					m_TASMode,
					x, y, width, height
					, g_FilmingStream_FfmpegEnable[FS_entity_right] ? g_FilmingStream_FfmpegOptions[FS_entity_right].c_str() : nullptr
				);
			}
		}

		if(bHudColor)
		{
			g_Filming_Stream[FS_hudcolor] = new FilmingStream(
				takePath, L"hudcolor",
				FB_COLOR,
				0.0, // Sampling not supported // samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_hudcolor] ? g_FilmingStream_FfmpegOptions[FS_hudcolor].c_str() : nullptr
			);
		}

		if(bHudAlpha)
		{
			g_Filming_Stream[FS_hudalpha] = new FilmingStream(
				takePath, L"hudalpha",
				FB_ALPHA,
				0.0, // Sampling not supported // samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_hudalpha] ? g_FilmingStream_FfmpegOptions[FS_hudalpha].c_str() : nullptr
			);
		}

		if(bDepthAll)
		{
			g_Filming_Stream[FS_depthall] = new FilmingStream(
				takePath, !m_EnableStereoMode ? L"depthall" : L"depthall_left",
				FB_DEPTH,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_depthall] ? g_FilmingStream_FfmpegOptions[FS_depthall].c_str() : nullptr
			);

			if(m_EnableStereoMode)
			{
				g_Filming_Stream[FS_depthall_right] = new FilmingStream(
					takePath, L"depthall_right",
					FB_DEPTH,
					samplingFrameDuration,
					m_TASMode,
					x, y, width, height
					, g_FilmingStream_FfmpegEnable[FS_depthall_right] ? g_FilmingStream_FfmpegOptions[FS_depthall_right].c_str() : nullptr
				);
			}
		}

		if(bDepthWorld)
		{
			g_Filming_Stream[FS_depthworld] = new FilmingStream(
				takePath, !m_EnableStereoMode ? L"depthworld" : L"depthworld_left",
				FB_DEPTH,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_depthworld] ? g_FilmingStream_FfmpegOptions[FS_depthworld].c_str() : nullptr
			);

			if(m_EnableStereoMode)
			{
				g_Filming_Stream[FS_depthworld_right] = new FilmingStream(
					takePath, L"depthworld_right",
					FB_DEPTH,
					samplingFrameDuration,
					m_TASMode,
					x, y, width, height
					, g_FilmingStream_FfmpegEnable[FS_depthworld_right] ? g_FilmingStream_FfmpegOptions[FS_depthworld_right].c_str() : nullptr
				);
			}
		}

		if(m_DebugCapture)
		{
			g_Filming_Stream[FS_debug] = new FilmingStream(
				takePath, L"debug",
				FB_COLOR,
				samplingFrameDuration,
				m_TASMode,
				x, y, width, height
				, g_FilmingStream_FfmpegEnable[FS_debug] ? g_FilmingStream_FfmpegOptions[FS_debug].c_str() : nullptr
			);
		}
	}

	m_CaptureEarly = (0.0 != movie_hidepanels->value) && !g_Filming_Stream[FS_hudcolor];
}

void Filming::Stop()
{
	if(g_Filming_Stream[FS_all]) delete g_Filming_Stream[FS_all];
	if(g_Filming_Stream[FS_all_right]) delete g_Filming_Stream[FS_all_right];
	if(g_Filming_Stream[FS_world]) delete g_Filming_Stream[FS_world];
	if(g_Filming_Stream[FS_world_right]) delete g_Filming_Stream[FS_world_right];
	if(g_Filming_Stream[FS_entity]) delete g_Filming_Stream[FS_entity];
	if(g_Filming_Stream[FS_entity_right]) delete g_Filming_Stream[FS_entity_right];
	if(g_Filming_Stream[FS_depthall]) delete g_Filming_Stream[FS_depthall];
	if(g_Filming_Stream[FS_depthall_right]) delete g_Filming_Stream[FS_depthall_right];
	if(g_Filming_Stream[FS_depthworld]) delete g_Filming_Stream[FS_depthworld];
	if(g_Filming_Stream[FS_depthworld_right]) delete g_Filming_Stream[FS_depthworld_right];
	if(g_Filming_Stream[FS_hudcolor]) delete g_Filming_Stream[FS_hudcolor];
	if(g_Filming_Stream[FS_hudalpha]) delete g_Filming_Stream[FS_hudalpha];
	if(g_Filming_Stream[FS_debug]) delete g_Filming_Stream[FS_debug];

	//

	if (_pSupportRender)
		_pSupportRender->hlaeOnFilmingStop();

	// stop camera motion export:
	g_CamExport.EndFiles();
	
	// stop sound system if exporting sound:
	if (_bExportingSound)
	{
		_FilmSound.Stop();
		_bExportingSound = false;
	}

	m_iFilmingState = FS_INACTIVE;
	_HudRqState=HUDRQ_NORMAL;

	// Need to reset this otherwise everything will run crazy fast
	// But not in TAS mode since it's not our business there
	if (!m_TASMode)
		pEngfuncs->Cvar_SetValue("host_framerate", 0);

	// in case our code is broken [again] we better also reset the mask here: : )
	glColorMask(TRUE, TRUE, TRUE, TRUE);
	glDepthMask(TRUE);

	if(g_AfxSettings.OptimizeCaptureVis_get())
		RedockGameWindow();

	// Print stats:
	{
		DWORD deltaTicks = GetTickCount() -m_StartTickCount;
		DWORD seconds = deltaTicks / 1000;
		DWORD minutes = seconds / 60;
		DWORD hours = minutes / 60;

		seconds = seconds % 60;
		minutes = minutes % 60;

		pEngfuncs->Con_Printf("Stats: %u engine frames / [%2u h %2u min %2u s] = %.4f eFPS.\n", m_HostFrameCount, hours, minutes, seconds, deltaTicks ? 1000 * m_HostFrameCount / (double)deltaTicks  : (m_HostFrameCount ? numeric_limits<float>::infinity() : 0.0));
	}
}


void LinearizeFloatDepthBuffer(GLfloat *pBuffer, unsigned int count, GLdouble zNear, GLdouble zFar) {

	GLfloat f = (GLfloat)zFar;
	GLfloat n = (GLfloat)zNear;
	GLfloat w = 1.0f;

	GLfloat f1 = (-1)*f*n*w;
	GLfloat f2 = f-n;

	for(; count; count--) {
		// y = (f1/(x*f2-f) -n)/f2
		*pBuffer = (f1/(*pBuffer * f2 -f)-n)/f2;
		pBuffer++;
	}
}

void InverseFloatDepthBuffer(GLfloat *pBuffer, unsigned int count, GLdouble zNear, GLdouble zFar) {

	GLfloat f = (GLfloat)zFar;
	GLfloat n = (GLfloat)zNear;
	GLfloat w = 1.0f;

	GLfloat f1 = (-1)*f*n*w;
	GLfloat f2 = f-n;

	for(; count; count--) {
		// y = (f1/(x*f2-f) -n)/f2
		// x = ((f1/(y*f2 +n))+f)/f2
		*pBuffer = (f1/(*pBuffer * f2 +n) +f)/f2;
		pBuffer++;
	}
}

void LogarithmizeDepthBuffer(GLfloat *pBuffer, unsigned int count, GLdouble zNear, GLdouble zFar) {
	GLfloat N  = (GLfloat)zNear;
	GLfloat yL = log((GLfloat)zNear);
	GLfloat yD = log((GLfloat)zFar) -yL;
	GLfloat xD = (GLfloat)zFar - (GLfloat)zNear;

	for(; count; count--) {
		*pBuffer = (log(xD*(*pBuffer) + N) -yL)/yD;
		pBuffer++;
	}

}

void DebugDepthBuffer(GLfloat *pBuffer, unsigned int count) {
	for(; count; count--) {
		float t = *pBuffer;
		if(t<0.0f) *pBuffer = 0.0f;
		else if(1.0f < t) *pBuffer = 1.0f;
		else *pBuffer = 0.5f;
		pBuffer++;
	}
}

void SliceDepthBuffer(GLfloat *pBuffer, unsigned int count, GLfloat sliceLo, GLfloat sliceHi) {

	if(!(
		0.0f <= sliceLo
		&& sliceLo < sliceHi
		&& sliceHi <= 1.0f
		&& (0.0f != sliceLo || 1.0f != sliceHi)
	))
		return; // no valid slicing range

	GLfloat s = 1.0f/(sliceHi - sliceLo);

	for(; count; count--) {
		GLfloat t = (*pBuffer);
		
		// clamp
		if(t<sliceLo) t = sliceLo;
		else if(sliceHi < t) t = sliceHi;
		
		*pBuffer = s*(t-sliceLo); // and scale
		
		pBuffer++;
	}
}

// Constraints: 
// - assumes the GLfloat buffer to contain values in [0.0f,1.0f] v
// - assumes GLfloat to conform with IEEE 754-2008 binary32
// - componentBytes \in 1,2,3
void GLfloatArrayToXByteArray(GLfloat *pBuffer, unsigned int width, unsigned int height, unsigned char componentBytes) {
	__asm {
		MOV  EBX, height
		TEST EBX, EBX
		JZ   __Done ; height 0

		MOV  EBX, width
		TEST EBX, EBX
		JZ   __Done ; width 0

		MOV  ESI, pBuffer
		MOV  EDI, ESI

		JMP  __SelectLoop

		__Next8:
			ADD  ESI, 4
			INC  EDI
			DEC  EBX
			JZ   __LineDone

		__Loop8:
			MOV  EAX, [ESI]
			TEST EAX, EAX
			JZ   __Zero8

			MOV  ECX, EAX
			SHR  ECX, 23
			SUB  CL, 127
			NEG  CL
			JZ   __One8

			; value in (0.0f, 1.0f)
			;
			AND  EAX, 0x7FFFFF
			OR   EAX, 0x800000
			SHR  EAX, 15
			SHR  EAX, CL
			MOV  [EDI], AL

			JMP  __Next8

		__Zero8:
			MOV  BYTE PTR [EDI], 0x00
			JMP  __Next8

		__One8:
			MOV  BYTE PTR [EDI], 0xFF
			JMP  __Next8

		__Next16:
			ADD  ESI, 4
			ADD  EDI, 2
			DEC  EBX
			JZ   __LineDone

		__Loop16:
			MOV  EAX, [ESI]
			TEST EAX, EAX
			JZ   __Zero16

			MOV  ECX, EAX
			SHR  ECX, 23
			SUB  CL, 127
			NEG  CL
			JZ   __One16

			; value in (0.0f, 1.0f)
			;
			AND  EAX, 0x7FFFFF
			OR   EAX, 0x800000
			SHR  EAX, 7
			SHR  EAX, CL
			MOV  [EDI], AX

			JMP  __Next16

		__Zero16:
			MOV  WORD PTR [EDI], 0x0000
			JMP  __Next16

		__One16:
			MOV  WORD PTR [EDI], 0xFFFF
			JMP  __Next16

		__Next24:
			ADD  ESI, 4
			ADD  EDI, 3
			DEC  EBX
			JZ   __LineDone

		__Loop24:
			MOV  EAX, [ESI]
			TEST EAX, EAX
			JZ   __Zero24

			MOV  ECX, EAX
			SHR  ECX, 23
			SUB  CL, 127
			NEG  CL
			JZ   __One24

			; value in (0.0f, 1.0f)
			;
			AND  EAX, 0x7FFFFF
			OR   EAX, 0x800000
			SHL  EAX, 1
			SHR  EAX, CL
			MOV  [EDI], AX
			SHR  EAX, 16
			MOV  [EDI+2], AL
			JMP  __Next24

		__Zero24:
			MOV  WORD PTR [EDI], 0x0000
			MOV  BYTE PTR [EDI+2], 0x0000
			JMP  __Next24

		__One24:
			MOV  WORD PTR [EDI], 0xFFFF
			MOV  BYTE PTR [EDI+2], 0xFF
			JMP  __Next24

		__LineDone:
			AND EBX, 0x00000003
			JZ   __LineDoneContinue

			; fix align:
			NEG BL
			ADD BL, 4
			ADD	EDI, EBX

		__LineDoneContinue:
			; check linecount:
			MOV  EBX, height
			DEC  EBX
			JZ   __Done

			MOV  height, ebx
			MOV  EBX, width

		__SelectLoop:
			MOV  AL, componentBytes
			CMP  AL, 1
			JLE  __Loop8
			CMP  AL, 2
			JLE  __Loop16
			JMP  __Loop24

			
		__Done:
	};
}


Filming::DRAW_RESULT Filming::shouldDraw(GLenum mode)
{
	bool bMatteXray = 0 != matte_xray->value ;
	bool bFilterEntities = matte_entities_r.bNotEmpty;

	int iMatteParticles = (int)matte_particles->value;
	bool bParticleWorld  = 0 != (0x01 & iMatteParticles);
	bool bParticleEntity = 0 != (0x02 & iMatteParticles);

	int iMatteViewModel = (int)matte_viewmodel->value;
	bool bViewModelWorld  = 0 != (0x01 & iMatteViewModel);
	bool bViewModelEntity = 0 != (0x02 & iMatteViewModel);

	int iMatteWmodels = (int)matte_worldmodels->value;
	bool bWmodelWorld  = 0 != (0x01 & iMatteWmodels);
	bool bWmodelEntity = 0 != (0x02 & iMatteWmodels);

	int iMatteEntityQuads = (int)matte_entityquads->value;
	bool bEntityQuadWorld  = 0 != (0x01 & iMatteEntityQuads);
	bool bEntityQuadEntity = 0 != (0x02 & iMatteEntityQuads);

	// in R_Particles:
	if(g_In_R_DrawParticles) {
		switch(m_iMatteStage) {
		case MS_WORLD:
			return bParticleWorld ? DR_NORMAL : DR_HIDE;
		case MS_ENTITY:
			return bParticleEntity ? DR_NORMAL : (bMatteXray ? DR_HIDE : DR_MASK );
		};
		return bParticleWorld | bParticleEntity ? DR_NORMAL : DR_HIDE;
	}

	// in R_DrawEntitiesOnList:
	else if(g_In_R_DrawEntitiesOnList) {
		cl_entity_t *ce = pEngStudio->GetCurrentEntity();

		if(!ce)
			return DR_NORMAL;

		// Apply entity Filter list:
		if (bFilterEntities) {
			bool bActive = _InMatteEntities(ce->index);
			switch(m_iMatteStage) {
			case MS_WORLD:
				return !bActive ? DR_NORMAL : DR_HIDE;
			case MS_ENTITY:
				return bActive ? DR_NORMAL : (bMatteXray ? DR_HIDE : DR_MASK );
			};
			return bActive ? DR_NORMAL : DR_HIDE;
		}

		// w_* models_:
		else if(ce->model && ce->model->type == mod_brush && strncmp(ce->model->name, "maps/", 5) != 0) {
			switch(m_iMatteStage) {
			case MS_WORLD:
				return bWmodelWorld ? DR_NORMAL : DR_HIDE;
			case MS_ENTITY:
				return bWmodelEntity ? DR_NORMAL : (bMatteXray ? DR_HIDE : DR_MASK );
			};
			return bWmodelWorld | bWmodelEntity ? DR_NORMAL : DR_HIDE;
		}

		// QUADS, such as blood sprites:
		else if(mode == GL_QUADS) {
			switch(m_iMatteStage) {
			case MS_WORLD:
				return bEntityQuadWorld ? DR_NORMAL : DR_HIDE;
			case MS_ENTITY:
				return bEntityQuadEntity ? DR_NORMAL : DR_HIDE;
			};
			return bEntityQuadWorld | bEntityQuadEntity ? DR_NORMAL : DR_HIDE;
		}

		// Everything else in the entity list:
		switch(m_iMatteStage) {
		case MS_WORLD:
			return DR_HIDE;
		case MS_ENTITY:
			return DR_NORMAL;
		};
		return DR_NORMAL;
	}

	// in R_DrawViewModel
	else if(g_In_R_DrawViewModel) {
		switch(m_iMatteStage) {
		case MS_WORLD:
			return bViewModelWorld ? DR_NORMAL : DR_HIDE;
		case MS_ENTITY:
			return bViewModelEntity ? DR_NORMAL : (bMatteXray ? DR_HIDE : DR_MASK );
		};
		return bViewModelWorld | bViewModelEntity ? DR_NORMAL : DR_HIDE;
	}

	// Everything else:
	switch(m_iMatteStage) {
	case MS_WORLD:
		return DR_NORMAL;
	case MS_ENTITY:
		return (bMatteXray ? DR_HIDE : DR_MASK );
	};
	return DR_NORMAL;
}

bool Filming::_InMatteEntities(int iid)
{
	bool bFound=false;

	std::list<int>::iterator iterend = matte_entities_r.ids.end();
	for (std::list<int>::iterator iter = matte_entities_r.ids.begin(); iter != iterend; iter++)
	{
		if (*iter == iid)
		{
			bFound=true;
			break;
		}
	}

	return bFound;
}

bool Filming::recordBuffers(HDC hSwapHDC,BOOL *bSwapRes)
// be sure to read the comments to _bRecordBuffers_FirstCall in filming.h, because this is fundamental for undertanding what the **** is going on here
// currently like the old code we relay on some user changable values, however we should lock those during filming to avoid crashes caused by the user messing around (not implemented yet)
{
	if (!_bRecordBuffers_FirstCall)
	{
		pEngfuncs->Con_Printf("WARNING: Unexpected recordBuffers request, this should not happen!");
	}
	_bRecordBuffers_FirstCall = false;

	double frameDuration;
	if (m_TASMode)
		frameDuration = pEngfuncs->pfnGetCvarFloat("host_framerate");
	else
		frameDuration = 1.0 / (double)m_fps;

	m_HostFrameCount++;

	// If we've only just started, delay until the next scene so that
	// the first frame is drawn correctly
	if (m_iFilmingState == FS_STARTING)
	{
		// we drop this frame and prepare the next one:

		m_iMatteStage = g_Filming_Stream[FS_all] ? MS_ALL : MS_WORLD;

		m_time += frameDuration;
		float fHostDuration = (float)(m_time - m_LastHostTime);
		m_LastHostTime += fHostDuration;

		if (!m_TASMode)
			pEngfuncs->Cvar_SetValue("host_framerate", fHostDuration);

		if (g_Filming_Stream[FS_hudcolor])
		{
			_HudRqState = HUDRQ_CAPTURE_COLOR; // signal we want an color capture
		}

		// execute swap already (support render might need preparations for first frame):
		if (_pSupportRender)
			*bSwapRes = _pSupportRender->hlaeSwapBuffers(hSwapHDC);
		else
			*bSwapRes = SwapBuffers(hSwapHDC);
		
		// prepare and clear for render:
		FullClear();

		m_iFilmingState = FS_ACTIVE;
		m_HudDrawnInFrame = false;

		_bRecordBuffers_FirstCall = true;
		return true;
	}

	// no sound updates during re-renders:
	FilmSound_BlockChannels(true);

	if(print_frame->value)
		pEngfuncs->Con_Printf("MDT: capturing engine frame #%i (mdt time: %f, client time: %f)\n", m_nFrames, m_time, pEngfuncs->GetClientTime());

	if (g_Filming_Stream[FS_hudcolor])
	{
		// since for HUD captures we waste a whole frame, request a new one:

		if(!m_HudDrawnInFrame)
		{
			// the hud hasn't been rendered for some reason, add
			// fake frames:

			// Swap for preview:
			if (_pSupportRender) *bSwapRes = _pSupportRender->hlaeSwapBuffers(hSwapHDC);
			else *bSwapRes=SwapBuffers(hSwapHDC);

			do {
				this->OnHudBeginEvent();
			} while(this->OnHudEndEvent());
		}

		_HudRqState = HUDRQ_NORMAL;

		// Delay:
		if (_bSimulate2 && movie_simulate_delay->value > 0) Sleep((DWORD)movie_simulate_delay->value);

		FullClear();
		Additional_R_RenderView(); // re-render view
	}

	m_HudDrawnInFrame = false;

	do
	{
		if(MS_ALL == m_iMatteStage)
		{
			if(_stereo_state == STS_RIGHT)
			{
				if(g_Filming_Stream[FS_all_right]) g_Filming_Stream[FS_all_right]->Capture(m_time, &m_GlRawPic, m_fps);
				if(g_Filming_Stream[FS_depthall_right]) g_Filming_Stream[FS_depthall_right]->Capture(m_time, &m_GlRawPic, m_fps);
			}
			else
			{
				if(!m_CaptureEarly && g_Filming_Stream[FS_all]) g_Filming_Stream[FS_all]->Capture(m_time, &m_GlRawPic, m_fps);
				if(!m_CaptureEarly && g_Filming_Stream[FS_depthall]) g_Filming_Stream[FS_depthall]->Capture(m_time, &m_GlRawPic, m_fps);
			}

			// Swap:
			if (_pSupportRender) *bSwapRes = _pSupportRender->hlaeSwapBuffers(hSwapHDC);
			else *bSwapRes=SwapBuffers(hSwapHDC);

			// Delay:
			if (_bSimulate2 && movie_simulate_delay->value > 0) Sleep((DWORD)movie_simulate_delay->value);
		}
		else
		{
			if(MS_WORLD == m_iMatteStage)
			{
				if(_stereo_state == STS_RIGHT)
				{
					if(g_Filming_Stream[FS_world_right]) g_Filming_Stream[FS_world_right]->Capture(m_time, &m_GlRawPic, m_fps);
					if(g_Filming_Stream[FS_depthworld_right]) g_Filming_Stream[FS_depthworld_right]->Capture(m_time, &m_GlRawPic, m_fps);
				}
				else
				{
					if(!m_CaptureEarly && g_Filming_Stream[FS_world]) g_Filming_Stream[FS_world]->Capture(m_time, &m_GlRawPic, m_fps);
					if(!m_CaptureEarly && g_Filming_Stream[FS_depthworld]) g_Filming_Stream[FS_depthworld]->Capture(m_time, &m_GlRawPic, m_fps);
				}

				// Swap:
				if (_pSupportRender) *bSwapRes = _pSupportRender->hlaeSwapBuffers(hSwapHDC);
				else *bSwapRes=SwapBuffers(hSwapHDC);

				// Delay:
				if (_bSimulate2 && movie_simulate_delay->value > 0) Sleep((DWORD)movie_simulate_delay->value);

				if(g_Filming_Stream[FS_entity] || g_Filming_Stream[FS_depthall])
				{
					m_iMatteStage = MS_ENTITY; // next is entity
					clearBuffers();
					Additional_R_RenderView(); // re-render view
				}
			}

			if(MS_ENTITY == m_iMatteStage)
			{
				if(_stereo_state == STS_RIGHT)
				{
					if(g_Filming_Stream[FS_entity_right]) g_Filming_Stream[FS_entity_right]->Capture(m_time, &m_GlRawPic, m_fps);
					if(g_Filming_Stream[FS_depthall_right]) g_Filming_Stream[FS_depthall_right]->Capture(m_time, &m_GlRawPic, m_fps);
				}
				else
				{
					if(g_Filming_Stream[FS_entity]) g_Filming_Stream[FS_entity]->Capture(m_time, &m_GlRawPic, m_fps);
					if(g_Filming_Stream[FS_depthall]) g_Filming_Stream[FS_depthall]->Capture(m_time, &m_GlRawPic, m_fps);
				}


				// Swap:
				if (_pSupportRender) *bSwapRes = _pSupportRender->hlaeSwapBuffers(hSwapHDC);
				else *bSwapRes=SwapBuffers(hSwapHDC);

				// Delay:
				if (_bSimulate2 && movie_simulate_delay->value > 0) Sleep((DWORD)movie_simulate_delay->value);
			}
		}

		m_iMatteStage = g_Filming_Stream[FS_all] ? MS_ALL : MS_WORLD;

		if (m_EnableStereoMode && (_stereo_state==STS_LEFT))
		{
			_stereo_state=STS_RIGHT;
			FullClear();
			Additional_R_RenderView(); // rerender frame instant!!!!
		} else _stereo_state=STS_LEFT;

	} while (_stereo_state!=STS_LEFT);
	
	//
	// Sound system handling:
	//

	if (_bExportingSound)
	{
		// is requested or active
		
		if (m_nFrames==0)
		{
			// first frame, start sound system!

			// try to init sound filming system:
			std::wstring fileName(m_TakeDir);
			std::wstring extraFileName(m_TakeDir);

			fileName.append(L"\\sound.wav");
			extraFileName.append(L"\\sound_extra.wav");

			_bExportingSound = _FilmSound.Start(fileName.c_str() , m_time, movie_sound_volume->value, extraFileName.c_str(), movie_sound_extra->value);

			if (!_bExportingSound) pEngfuncs->Con_Printf("ERROR: Starting MDT Sound Recording System failed!\n");

		} else {
			// advancing frame, update sound system

			_FilmSound.AdvanceFrame(m_time);
		}
	}

	m_nFrames++;
	m_time += frameDuration;
	float fHostDuration = (float)(m_time - m_LastHostTime);
	m_LastHostTime += fHostDuration;
	
	if (!m_TASMode)
		pEngfuncs->Cvar_SetValue("host_framerate", fHostDuration);

	_bRecordBuffers_FirstCall = true;

	if (g_Filming_Stream[FS_hudcolor] && isFilming())
	{
		_HudRqState = HUDRQ_CAPTURE_COLOR; // signal we want an color capture
	}

	// allow sound updates again:
	FilmSound_BlockChannels(false);

	return true;
}


void Filming::FullClear()
{
	// Make sure the mask colour is still correct
	glClearColor(m_MatteColour[0], m_MatteColour[1], m_MatteColour[2], 1.0f);
	glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
}

void Filming::clearBuffers()
{
	// Make sure the mask colour is still correct
	glClearColor(m_MatteColour[0], m_MatteColour[1], m_MatteColour[2], 1.0f);

	// Now we do our clearing!
	if(m_iMatteStage!=MS_ENTITY || matte_xray->value || MM_ALPHA == m_MatteMethod)
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
	else
		glClear(GL_COLOR_BUFFER_BIT);
}

bool Filming::checkClear(GLbitfield mask)
{
	// we now want coll app controll
	// Don't clear unless we specify
	if (isFilming() && (mask & GL_COLOR_BUFFER_BIT || mask & GL_DEPTH_BUFFER_BIT))
		return false;

	// Make sure the mask colour is still correct
	glClearColor(m_MatteColour[0], m_MatteColour[1], m_MatteColour[2], 1.0f);
	// we could also force glDepthRange here, but I preffer relaing on that forcing ztrick 0 worked

	return true;
}

Filming::DRAW_RESULT Filming::doWireframe(GLenum mode)
{
	// Wireframe turned off
	if (m_bInWireframe && movie_wireframe->value == 0)
	{
		glLineWidth(1.0f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		m_bInWireframe = false;
		return DR_NORMAL;
	}
	
	if (movie_wireframe->value == 0)
		return DR_NORMAL;

	m_bInWireframe = true;

	// Keep the same mode as before
	if (mode == m_iLastMode)
		return DR_NORMAL;

	// Record last mode, but record STRIPS for FANS (since they imply the same
	// things in terms of wireframeness.
	m_iLastMode = (mode == GL_TRIANGLE_FAN ? GL_TRIANGLE_STRIP : mode);

	if (movie_wireframe->value == 1 && mode == GL_QUADS)
		return DR_HIDE;
	
	if (movie_wireframe->value == 1 && mode == GL_POLYGON)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else if (movie_wireframe->value == 2 && (mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN))
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else if (movie_wireframe->value == 3)
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	else
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	glLineWidth(movie_wireframesize->value);

	return DR_NORMAL;
}

void Filming::DoWorldFxBegin(GLenum mode)
{
	static float psCurColor[4];

	// those are wh effects

	// only if transparency is enabled world and not in entitymatte
	if ( fx_wh_enable->value==0.0f || mode == GL_TRIANGLE_STRIP || mode == GL_TRIANGLE_FAN || m_iMatteStage == Filming::MS_ENTITY )
	{
		_bWorldFxDisableBlend=false;
		_bWorldFxEnableDepth=false;
		return;
	}

	if (fx_wh_tint_enable->value==0.0f)
	{
		glGetFloatv(GL_CURRENT_COLOR,psCurColor);
		glColor4f(psCurColor[0],psCurColor[1],psCurColor[2],fx_wh_alpha->value);
	}
	else
		glColor4f(_fx_whRGBf[0],_fx_whRGBf[1],_fx_whRGBf[2],fx_wh_alpha->value);


	glDisable(GL_DEPTH_TEST);
	_bWorldFxEnableDepth=true;
	if (mode==GL_QUADS)
	{
		if (fx_wh_noquads->value==1.0f)
			glBlendFunc(GL_ZERO,GL_ONE);
		//else default blend func
	} else 	{
		if (fx_wh_additive->value==1.0f)
			glBlendFunc(GL_SRC_ALPHA,GL_ONE);
		else
			glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}

	glEnable(GL_BLEND);
	_bWorldFxDisableBlend=true;
}

void Filming::DoWorldFxEnd()
{
	// those are wh effects

	if (_bWorldFxDisableBlend)
	{
		glDisable(GL_BLEND);
		_bWorldFxDisableBlend = false;
	}
	if (_bWorldFxEnableDepth)
	{
		glEnable(GL_DEPTH_TEST);
		_bWorldFxEnableDepth = false;
	}
}

void Filming::setWhTintColor(float r, float g, float b)
{
	_fx_whRGBf[0]=r;
	_fx_whRGBf[1]=g;	
	_fx_whRGBf[2]=b;
}

void Filming::DoWorldFx2(GLenum mode)
{
	if(g_In_R_DrawViewModel && fx_viewmodel_lightmap->value)
	{
		if (fx_viewmodel_lightmap->value==2.0f)
			glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
		else
			glBindTexture(GL_TEXTURE_2D,0);
	}

	// only if enabled and world and not in entity matte
	if ( fx_lightmap->value==0.0f || mode != GL_POLYGON || m_iMatteStage == Filming::MS_ENTITY )
		return;

	if (fx_lightmap->value==2.0f)
		glBindTexture(GL_TEXTURE_2D,0);
	else
		glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
}

// FilmingStream ///////////////////////////////////////////////////////////////

FilmingStream::FilmingStream(
	wchar_t const * takePath, wchar_t const * name,
	FILMING_BUFFER buffer,
	double samplingFrameDuration,
	bool TASMode,
	int x, int y, int width, int height,
	const wchar_t * ffMpegOptions)
{
	if (ffMpegOptions) m_FfmpegOptions = ffMpegOptions;

	size_t nameBufferLength = wcslen(name) +1;

	m_Bmp = 0.0f != movie_bmp->value;
	m_Buffer = buffer;

	unsigned char ucDepthDump = (unsigned char)movie_depthdump->value;

	m_DepthDebug = 0 != (0x4 & ucDepthDump);

	switch(ucDepthDump)
	{
	case 2:
		m_DepthFn = FD_LOG;
		break;
	case 3:
		m_DepthFn = FD_INV;
		break;
	default:
		m_DepthFn = FD_LINEAR;
	}

	m_DepthSliceLo = depth_slice_lo->value;
	m_DepthSliceHi = depth_slice_hi->value;

	if(m_DepthSliceLo < 0.0f) m_DepthSliceLo = 0.0f;
	if(1.0f < m_DepthSliceLo) m_DepthSliceLo = 1.0f;

	if(m_DepthSliceHi < 0.0f) m_DepthSliceHi = 0.0f;
	if(1.0f < m_DepthSliceHi) m_DepthSliceHi = 1.0f;

	if(m_DepthSliceHi < m_DepthSliceLo) m_DepthSliceHi = m_DepthSliceLo;

	if(m_DepthSliceLo == m_DepthSliceHi)
	{
		m_DepthSliceLo = 0.0f;
		m_DepthSliceHi = 1.0f;
	}

	m_DirCreated = false;
	m_FrameCount = 0;
	m_Sampler = 0;
	m_Width = width;
	m_Height = height;
	m_X = x;
	m_Y = y;
	m_TASMode = TASMode;
	m_NextFrameIsAt = 0.0;

	m_Path.assign(takePath);
	m_Path.append(L"\\");
	m_Path.append(name);

	if(!g_Filming.GetSimulate2())
	{
		m_DirCreated = CreatePath(m_Path.c_str(), m_Path);
		if(!m_DirCreated)
			pEngfuncs->Con_Printf("ERROR: could not create \"%s\"\n", m_Path.c_str());
	}

	switch(buffer) {
	case FB_ALPHA:
		m_GlBuffer = GL_ALPHA;
		m_GlType = GL_UNSIGNED_BYTE;
		m_BytesPerPixel = 1;
		break;

	case FB_DEPTH:
		m_GlBuffer = GL_DEPTH_COMPONENT;
		m_GlType = GL_FLOAT;
		if(0 != depth_exr->value)
			m_BytesPerPixel = 4;
		else
		{
			switch ((unsigned char)depth_bpp->value) {
			case 16:
				m_BytesPerPixel = 2;
				break;
			case 24:
				m_BytesPerPixel = 3;
				break;
			default:
				m_BytesPerPixel = 1;
			}
		}
		break;

	case FB_COLOR:
	default:
		m_GlBuffer = GL_BGR_EXT;
		m_GlType = GL_UNSIGNED_BYTE;
		m_BytesPerPixel = 3;
	};

	m_Pitch = CalcPitch(m_Width, m_BytesPerPixel, m_Bmp ? 4 : 1);

	if(0 < samplingFrameDuration && (FB_COLOR == buffer || FB_ALPHA == buffer))
	{
		// activate sampling.

		EasySamplerSettings settings(
			m_BytesPerPixel * width, height,
			0 == sample_smethod->value ? EasySamplerSettings::ESM_Rectangle : EasySamplerSettings::ESM_Trapezoid,
			samplingFrameDuration,
			0,
			sample_exposure->value,
			sample_frame_strength->value
		);

		m_Sampler = new EasyByteSampler(
			settings,
			m_Pitch,
			static_cast<IFramePrinter *>(this)
		);
	}
	else
		m_Sampler = 0;


	if(0 < samplingFrameDuration && FB_DEPTH == buffer)
	{
		// activate sampling.

		EasySamplerSettings settings(
			width, height,
			0 == sample_smethod->value ? EasySamplerSettings::ESM_Rectangle : EasySamplerSettings::ESM_Trapezoid,
			samplingFrameDuration,
			0,
			sample_exposure->value,
			sample_frame_strength->value
		);

		m_SamplerFloat = new EasyFloatSampler(
			settings,
			static_cast<IFloatFramePrinter *>(this)
		);
	}
	else
		m_SamplerFloat = 0;

}

FilmingStream::~FilmingStream()
{
	if(m_Sampler) delete m_Sampler;
	if (m_FfmpegOutStream) m_FfmpegOutStream->Release();
}

void FilmingStream::Capture(double time, CMdt_Media_RAWGLPIC * usePic, float spsHint)
{
	if(m_Sampler && 0 < spsHint && m_Sampler->CanSkipConstant(time, 1.0 / (double)spsHint))
	{
		// we can skip the capture safely:
		m_Sampler->Sample(
			0,
			time
		);
		//pEngfuncs->Con_Printf("skipped at %f\n", time);
		return;
	}

	if (!usePic->DoGlReadPixels(m_X, m_Y, m_Width, m_Height, m_GlBuffer, m_GlType, !m_Bmp))
	{
		pEngfuncs->Con_Printf("MDT ERROR: failed to capture a frame (%d).\n", usePic->GetLastUnhandledError());
		return;
	}

	// apply postprocessing to the depthbuffer:
	// the following code should be replaced completly later, cause it's rather dependent
	// on sizes of unsigned int and float and stuff, although it should be somewhat
	// save from acces violations (only corrupted pixel data):
	// also the GL_UNSIGNED_INT FIX is somewhat slow by now, code has to be optimized
	if (FB_DEPTH == m_Buffer)
	{
		// user wants depth output, we need to cut off
		unsigned int uiCount = (unsigned int)m_Width * (unsigned int)m_Height;
		void * pBuffer = usePic->GetPointer();	// the pointer where we write

		if(0 != m_SamplerFloat)
		{
			LinearizeFloatDepthBuffer((GLfloat *)pBuffer, uiCount, g_Filming.GetZNear(), g_Filming.GetZFar());
		}
		else
		{
			if(FD_LINEAR == m_DepthFn || FD_LOG == m_DepthFn)
				LinearizeFloatDepthBuffer((GLfloat *)pBuffer, uiCount, g_Filming.GetZNear(), g_Filming.GetZFar());

			if(FD_LOG == m_DepthFn)
				LogarithmizeDepthBuffer((GLfloat *)pBuffer, uiCount, g_Filming.GetZNear(), g_Filming.GetZFar());

			if(m_DepthDebug)
				DebugDepthBuffer((GLfloat *)pBuffer, uiCount);

			if(0.0f != m_DepthSliceLo || 1.0f != m_DepthSliceHi)
				SliceDepthBuffer((GLfloat *)pBuffer, uiCount, m_DepthSliceLo, m_DepthSliceHi);

			if(0 == depth_exr->value) GLfloatArrayToXByteArray((GLfloat *)pBuffer, m_Width, m_Height, m_BytesPerPixel);
		}	
	}

	if (!m_TASMode)
	{
		WriteFrame(*usePic, time);
	}
	else
	{
		if (m_NextFrameIsAt == 0.0)
		{
			m_PreviousFrame = *usePic;
		}

		size_t framesWritten = 0;

		/*
		 * If the player is seeing the current engine frame for more time than
		 * the previous engine frame during the current video frame, use the current engine frame.
		 *
		 * This condition can be written as:
		 * if the current engine time is closer to the current video frame time
		 * than half of the video framerate.
		 */
		while (time - m_NextFrameIsAt > (1.0 / (spsHint * 2.0)))
		{
			WriteFrame(m_PreviousFrame, m_NextFrameIsAt);
			m_NextFrameIsAt += (1.0 / spsHint);

			++framesWritten;
		}

		m_PreviousFrame = *usePic;

		if (m_NextFrameIsAt <= time)
		{
			WriteFrame(m_PreviousFrame, m_NextFrameIsAt);
			m_NextFrameIsAt += (1.0 / spsHint);

			++framesWritten;
		}

		if (print_frame->value)
		{
			if (framesWritten > 0)
			{
				pEngfuncs->Con_Printf(
					"Wrote %lu frame%s (time = %.8f, m_NextFrameIsAt = %.8f).\n",
					framesWritten,
					framesWritten > 1 ? "s" : "",
					time,
					m_NextFrameIsAt);
			} else
			{
				pEngfuncs->Con_Printf("Skipping a frame (time = %.8f, m_NextFrameIsAt = %.8f).\n", time, m_NextFrameIsAt);
			}
		}
	}
}

void FilmingStream::WriteFrame(CMdt_Media_RAWGLPIC& frame, double time)
{
	if (0 != m_Sampler)
	{
		// pass on to sampling system:

		m_Sampler->Sample(
			(unsigned char const *)frame.GetPointer(),
			time
		);
	}
	else if (0 != m_SamplerFloat)
	{
		// pass on to sampling system:

		m_SamplerFloat->Sample(
			(float const *)frame.GetPointer(),
			time
		);
	}
	else
	{
		// write out directly:

		if (FB_DEPTH == m_Buffer && 0 != depth_exr->value)
		{
			PrintExr((float*)frame.GetPointer());
		}
		else
		{
			Print((unsigned char const*)frame.GetPointer());
		}
	}
}

void FilmingStream::Print(unsigned char const * data)
{
	if(!m_DirCreated)
		return;

	bool bColor = m_Buffer == FB_COLOR;
	
	if (!m_FfmpegOptions.empty())
	{
		advancedfx::ImageFormat format = advancedfx::ImageFormat::Unkown;

		switch (m_BytesPerPixel)
		{
		case 1:
			format = advancedfx::ImageFormat::A;
			break;
		case 3:
			format = advancedfx::ImageFormat::BGR;
			break;
		case 4:
			format = advancedfx::ImageFormat::BGRA;
			break;
		}

		advancedfx::CImageFormat imageFormat(
			format, m_Width, m_Height, m_Pitch);

		if (nullptr == m_FfmpegOutStream)
		{
			m_FfmpegOutStream = new advancedfx::COutFFMPEGVideoStream(imageFormat, m_Path, m_FfmpegOptions, movie_fps->value);
			m_FfmpegOutStream->AddRef();
		}
		
		advancedfx::CImageBuffer buffer;
		buffer.Format = imageFormat;
		buffer.Buffer = const_cast<unsigned char *>(data); //TODO: not nice this cast.

		if (!m_FfmpegOutStream->SupplyVideoData(buffer))
		{
			std::string path("n/a");
			WideStringToUTF8String(m_Path.c_str(), path);
			pEngfuncs->Con_Printf("AFXERROR: Failed writing image to stream for \"%s\".\n", path.c_str());
		}

		buffer.Buffer = nullptr;
	}	
	else
	{
		std::wostringstream os;
		os << m_Path << L"\\" << setfill(L'0') << setw(5) << m_FrameCount << setw(0) << (m_Bmp ? L".bmp" : L".tga");
		
		if (m_Bmp)
			WriteRawBitmap(data, os.str().c_str(), m_Width, m_Height, m_BytesPerPixel << 3, m_Pitch); // align is still 4 byte probably
		else
			WriteRawTarga(data, os.str().c_str(), m_Width, m_Height, m_BytesPerPixel << 3, !bColor, m_Pitch);
	}

	m_FrameCount++;
}

void FilmingStream::PrintExr(float const* data)
{
	if (!m_DirCreated)
		return;

	std::wostringstream os;
	os << m_Path << L"\\" << setfill(L'0') << setw(5) << m_FrameCount << setw(0) << L".exr";

	WriteFloatZOpenExr(
		os.str().c_str(),
		(unsigned char*)data,
		m_Width,
		m_Height,
		sizeof(float),
		m_Pitch,
		2 == depth_exr->value ? WFZOEC_Zip : WFZOEC_None,
		false
	);

	m_FrameCount++;
}

void FilmingStream::Print(float const * data)
{
	if(FD_LINEAR != m_DepthFn || m_DepthDebug)
	{
		unsigned int uiCount = m_Height * m_Width;

		// (allocation / sharing could be optimized later)
		GLfloat * fT = new GLfloat[uiCount];

		memcpy(fT, data, uiCount * sizeof(float));

		if(FD_INV == m_DepthFn)
			InverseFloatDepthBuffer(fT, uiCount, g_Filming.GetZNear(), g_Filming.GetZFar());

		if(FD_LOG == m_DepthFn)
			LogarithmizeDepthBuffer(fT, uiCount, g_Filming.GetZNear(), g_Filming.GetZFar());

		if(m_DepthDebug)
			DebugDepthBuffer(fT, uiCount);

		if(0.0f != m_DepthSliceLo || 1.0f != m_DepthSliceHi)
			SliceDepthBuffer(fT, uiCount, m_DepthSliceLo, m_DepthSliceHi);

		if (0 == depth_exr->value)
		{
			GLfloatArrayToXByteArray(fT, m_Width, m_Height, m_BytesPerPixel);

			Print((unsigned char const*)fT);
		}
		else
		{
			PrintExr(fT);
		}

		delete fT;
	}
	else
	{
		if (0 == depth_exr->value)
		{
			GLfloatArrayToXByteArray((GLfloat*)data, m_Width, m_Height, m_BytesPerPixel);
			Print((unsigned char const*)data);
		}
		else
		{
			PrintExr((float*)data);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

REGISTER_CMD_FUNC_BEGIN(recordmovie)
{
	if (g_Filming.isFilming())
	{
		pEngfuncs->Con_Printf("Already recording!\n");
		return;
	}

	g_Filming.Start();
}

REGISTER_CMD_FUNC_END(recordmovie)
{
	if(!g_Filming.isFilming())
		return;

	g_Filming.Stop();
}

REGISTER_CMD_FUNC(recordmovie_start)
{
	CALL_CMD_BEGIN(recordmovie);
}

REGISTER_CMD_FUNC(recordmovie_stop)
{
	CALL_CMD_END(recordmovie);
}

void _mirv_matte_setcolorf(float flRed, float flBlue, float flGreen)
{
	// ensure that the values are within the falid range
	clamp(flRed, 0.0f, 1.0f);
	clamp(flGreen, 0.0f, 1.0f);	
	clamp(flBlue, 0.0f, 1.0f);
	// store matte values.
	g_Filming.setMatteColour(flRed, flGreen, flBlue);
}

// that's not too nice, may be someone can code it more efficient (but still readable please):
// also I think you can retrive it directly as float or even dot it as an cvars
REGISTER_CMD_FUNC(matte_setcolor)
{
	if (pEngfuncs->Cmd_Argc() != 4)
	{
		pEngfuncs->Con_Printf("Useage: " PREFIX "matte_setcolor <red: 0-255> <green: 0-255> <blue: 0-255>\n");
		return;
	}

	float flRed = (float) atoi(pEngfuncs->Cmd_Argv(1)) / 255.0f;
	float flGreen = (float) atoi(pEngfuncs->Cmd_Argv(2)) / 255.0f;
	float flBlue = (float) atoi(pEngfuncs->Cmd_Argv(3)) / 255.0f;

	_mirv_matte_setcolorf(flRed, flBlue, flGreen);
}

REGISTER_CMD_FUNC(matte_setcolorf)
{
	if (pEngfuncs->Cmd_Argc() != 4)
	{
		pEngfuncs->Con_Printf("Useage: " PREFIX "matte_setcolorf <red: 0.0-1.0> <green: 0.0-1.0> <blue: 0.0-1.0>\n");
		return;
	}

	float flRed = (float) atof(pEngfuncs->Cmd_Argv(1));
	float flGreen = (float) atof(pEngfuncs->Cmd_Argv(2));
	float flBlue = (float) atof(pEngfuncs->Cmd_Argv(3));

	_mirv_matte_setcolorf(flRed, flBlue, flGreen);
}

REGISTER_CMD_FUNC(fx_wh_tint_colorf)
{
	if (pEngfuncs->Cmd_Argc() != 4)
	{
		pEngfuncs->Con_Printf("Useage: " PREFIX "wh_tint_colorf <red: 0.0-1.0> <green: 0.0-1.0> <blue: 0.0-1.0>\n");
		return;
	}

	float flRed = (float) atof(pEngfuncs->Cmd_Argv(1));
	float flGreen = (float) atof(pEngfuncs->Cmd_Argv(2));
	float flBlue = (float) atof(pEngfuncs->Cmd_Argv(3));

	// ensure that the values are within the falid range
	g_Filming.setWhTintColor(clamp(flRed, 0.0f, 1.0f),clamp(flGreen, 0.0f, 1.0f),clamp(flBlue, 0.0f, 1.0f));
}

REGISTER_CMD_FUNC(matte_entities)
{
	bool bShowHelp=true;
	int icarg=pEngfuncs->Cmd_Argc();

	if (icarg>=2)
	{
		char *pcmd = pEngfuncs->Cmd_Argv(1);

		if (!lstrcmp(pcmd ,"list") && icarg==2)
		{
			// list
			pEngfuncs->Con_Printf("Ids: ");
			std::list<int>::iterator iterend = g_Filming.matte_entities_r.ids.end();
			for (std::list<int>::iterator iter = g_Filming.matte_entities_r.ids.begin(); iter != iterend; iter++)
			{
				pEngfuncs->Con_Printf("%i, ",*iter);
			}
			pEngfuncs->Con_Printf("\n");
			bShowHelp=false;
		}
		else if (!lstrcmp(pcmd ,"add") && icarg==3)
		{
			// add
			int iid = atoi(pEngfuncs->Cmd_Argv(2));
			g_Filming.matte_entities_r.ids.push_front(iid);
			g_Filming.matte_entities_r.bNotEmpty=true;
			bShowHelp=false;
		}
		else if (!lstrcmp(pcmd ,"del") && icarg==3)
		{
			// del
			int iid = atoi(pEngfuncs->Cmd_Argv(2));
			g_Filming.matte_entities_r.ids.remove(iid);
			g_Filming.matte_entities_r.bNotEmpty=!(g_Filming.matte_entities_r.ids.empty());
			bShowHelp=false;
		}
		else if (!lstrcmp(pcmd ,"clear") && icarg==2)
		{
			// clear
			g_Filming.matte_entities_r.ids.clear();
			g_Filming.matte_entities_r.bNotEmpty=false;
			bShowHelp=false;
		}
	}

	if (bShowHelp)
	{
		pEngfuncs->Con_Printf(
			"Allows to restrict the matte to a list of entities only.\n"
			"If the list is empty (default) all show up.\n"
			"\n"
			"Commands:\n"
			"\t" PREFIX "matte_entities list - displays current list\n"
			"\t" PREFIX "matte_entities add <id> - adds entity with id <id> to the list\n"
			"\t" PREFIX "matte_entities del <id> - removes <id> from the list\n"
			"\t" PREFIX "matte_entities clear - clears all items from the list\n"
		);
	}
}


REGISTER_DEBUGCMD_FUNC(depth_info) {
	GLdouble N = g_Filming.GetZNear();
	GLdouble F = g_Filming.GetZFar();
	GLdouble E = (F-N)/256.0f;
	GLdouble P = (F-N) ? 100*E/(F-N) : 0;
	pEngfuncs->Con_Printf("zNear: %f\nzFar: %f\nMax linear error (8bit): %f (%f %%)\n", N, F, E, P);
}

REGISTER_CMD_FUNC(fov)
{
	int argc = pEngfuncs->Cmd_Argc();;

	if(2 <= argc)
	{
		char const * arg1 = pEngfuncs->Cmd_Argv(1);

		if(0 == _stricmp("default", arg1))
		{
			g_Filming.FovDefault();
			return;
		}
		else
		{
			g_Filming.FovOverride(atof(arg1));
			return;
		}
	}

	pEngfuncs->Con_Printf(
		"Usage:\n"
		PREFIX "fov f - Override fov with given floating point value (f).\n"
		PREFIX "fov default - Revert to the game's default behaviour.\n"
	);
}

REGISTER_CMD_FUNC(movie_ffmpeg)
{
	int argC = pEngfuncs->Cmd_Argc();
	const char* arg0 = pEngfuncs->Cmd_Argv(0);

	if (3 <= argC)
	{
		std::list<int> selectedStreams;
		const char* arg1 = pEngfuncs->Cmd_Argv(1);
		const char* arg2 = pEngfuncs->Cmd_Argv(2);

		if (0 == _stricmp("all", arg1))
		{
			selectedStreams.push_back(FS_all);
			selectedStreams.push_back(FS_all_right);
			selectedStreams.push_back(FS_world);
			selectedStreams.push_back(FS_world_right);
			selectedStreams.push_back(FS_entity);
			selectedStreams.push_back(FS_entity_right);
			selectedStreams.push_back(FS_depthall);
			selectedStreams.push_back(FS_depthall_right);
			selectedStreams.push_back(FS_depthworld);
			selectedStreams.push_back(FS_depthworld_right);
			selectedStreams.push_back(FS_hudcolor);
			selectedStreams.push_back(FS_hudalpha);
			selectedStreams.push_back(FS_debug);
		}
		else if (0 == _stricmp("allMain", arg1))
		{
			selectedStreams.push_back(FS_all);
			selectedStreams.push_back(FS_all_right);
		}
		else if (0 == _stricmp("allWorld", arg1))
		{
			selectedStreams.push_back(FS_world);
			selectedStreams.push_back(FS_world_right);
		}
		else if (0 == _stricmp("allEntity", arg1))
		{
			selectedStreams.push_back(FS_entity);
			selectedStreams.push_back(FS_entity_right);
		}
		else if (0 == _stricmp("allDepth", arg1))
		{
			selectedStreams.push_back(FS_depthall);
			selectedStreams.push_back(FS_depthall_right);
			selectedStreams.push_back(FS_depthworld);
			selectedStreams.push_back(FS_depthworld_right);
		}
		else if (0 == _stricmp("main", arg1))
		{
			selectedStreams.push_back(FS_all);
		}
		else if (0 == _stricmp("mainRight", arg1))
		{
			selectedStreams.push_back(FS_all_right);
		}
		else if (0 == _stricmp("world", arg1))
		{
			selectedStreams.push_back(FS_world);
		}
		else if (0 == _stricmp("worldRight", arg1))
		{
			selectedStreams.push_back(FS_world_right);
		}
		else if (0 == _stricmp("entity", arg1))
		{
			selectedStreams.push_back(FS_entity);
		}
		else if (0 == _stricmp("entityRight", arg1))
		{
			selectedStreams.push_back(FS_entity_right);
		}
		else if (0 == _stricmp("depthMain", arg1))
		{
			selectedStreams.push_back(FS_depthall);
		}
		else if (0 == _stricmp("depthMainRight", arg1))
		{
			selectedStreams.push_back(FS_depthall_right);
		}
		else if (0 == _stricmp("depthWorld", arg1))
		{
			selectedStreams.push_back(FS_depthworld);
		}
		else if (0 == _stricmp("depthWorldRight", arg1))
		{
			selectedStreams.push_back(FS_depthworld_right);
		}
		else if (0 == _stricmp("hudColor", arg1))
		{
			selectedStreams.push_back(FS_hudcolor);
		}
		else if (0 == _stricmp("hudAlpha", arg1))
		{
			selectedStreams.push_back(FS_hudalpha);
		}
		else if (0 == _stricmp("debug", arg1))
		{
			selectedStreams.push_back(FS_debug);
		}

		if (!selectedStreams.empty())
		{
			if (0 == _stricmp("enabled", arg2))
			{
				if (4 == argC)
				{
					bool enable = 0 != atoi(pEngfuncs->Cmd_Argv(3));

					for (std::list<int>::iterator it = selectedStreams.begin(); it != selectedStreams.end(); ++it)
					{
						g_FilmingStream_FfmpegEnable[*it] = enable;
					}
					return;
				}

				if (1 == selectedStreams.size())
				{
					pEngfuncs->Con_Printf("%s %s enabled = %i\n", arg0, arg1, g_Filming_Stream[selectedStreams.front()] ? 1 : 0);
					return;
				}
			}
			else if (0 == _stricmp("options", arg2))
			{
				if (4 == argC)
				{
					std::wstring wOptions;
					if (!UTF8StringToWideString(pEngfuncs->Cmd_Argv(3), wOptions))
					{
						pEngfuncs->Con_Printf("Error, can not convert \"%s\" from UTF8 to wide string.\n", pEngfuncs->Cmd_Argv(3));
						return;
					}

					for (std::list<int>::iterator it = selectedStreams.begin(); it != selectedStreams.end(); ++it)
					{
						g_FilmingStream_FfmpegOptions[*it] = wOptions;
					}
					return;
				}

				if (1 == selectedStreams.size())
				{
					std::string options;
					if(!WideStringToUTF8String(g_FilmingStream_FfmpegOptions[selectedStreams.front()].c_str(), options))
					{
						pEngfuncs->Con_Printf("Error, can not convert wide string to UTF8.\n", pEngfuncs->Cmd_Argv(3));
						return;
					}

					pEngfuncs->Con_Printf("%s %s options = \"%s\"\n", arg0, arg1, options.c_str());
					return;
				}
			}
		}
	}

	pEngfuncs->Con_Printf(
		"%s all|allColor|allMain|allWorld|allEntity|allDepth enabled 0|1 - Disables/enables FFMPEG for the given group of streams.\n"
		, arg0
	);
	pEngfuncs->Con_Printf(
		"%s main|mainRight|world|worldRight|entity|entityRight|depthMain|depthMainRight|depthWorld|depthWorldRight|hudColor|hudAlpha|debug enabled [0|1] - Gets or sets current enabled state on the given stream.\n"
		, arg0
	);
	pEngfuncs->Con_Printf(
		"%s all|allColor|allMain|allWorld|allEntity|allDepth options <sOptions> - Sets <sOptions> for the given group of streams.\n"
		, arg0
	);
	pEngfuncs->Con_Printf(
		"%s main|mainRight|world|worldRight|entity|entityRight|depthMain|depthMainRight|depthWorld|depthWorldRight|hudColor|hudAlpha|debug options [<sOptions>] - Gets or sets options on the given stream.\n"
		, arg0
	);
}
